using UnityEngine;
using TMPro;

public class ShootingSystem : MonoBehaviour
{
    public static ShootingSystem Instance;

    [Header("Settings")]
    public LayerMask enemyLayer;
    public float raycastDistance = 100f;
    public int maxAmmo = 10;
    public float reloadTime = 2f;

    [Header("UI")]
    public TextMeshProUGUI ammoP1Text;
    public TextMeshProUGUI ammoP2Text;

    private bool[] _triggerHeld = new bool[2];
    private int[] _ammo = new int[2];
    private float[] _reloadTimer = new float[2];
    private bool[] _isReloading = new bool[2];
    private bool[] _triggerFiredOnEmpty = new bool[2];

    [Header("Hit Detection")]
    public float hitTolerancePixels = 3f;  // radius of hit area in pixels

    void Awake()
    {
        if (Instance != null)
        {
            Destroy(gameObject);
            return;
        }
        Instance = this;
        _ammo[0] = maxAmmo;
        _ammo[1] = maxAmmo;
        UpdateAmmoUI();
    }

    void Update()
    {
        if (GunInputReader.Instance == null) return;

        for (int i = 0; i < 2; i++)
        {
            var input = GunInputReader.Instance.players[i];

            if (!input.isConnected)
            {
                _triggerHeld[i] = false;
                continue;
            }

            HandleReload(i, input);
            HandleShooting(i, input);
        }
    }

    void HandleReload(int playerIndex, GunInputReader.PlayerInput input)
    {
        if (input.trackingLost || _triggerFiredOnEmpty[playerIndex])
        {
            _reloadTimer[playerIndex] += Time.deltaTime;

            if (!_isReloading[playerIndex])
            {
                _isReloading[playerIndex] = true;
            }

            if (_reloadTimer[playerIndex] >= reloadTime)
            {
                _ammo[playerIndex] = maxAmmo;
                _reloadTimer[playerIndex] = 0f;
                _isReloading[playerIndex] = false;
                _triggerFiredOnEmpty[playerIndex] = false;
                UpdateAmmoUI();
                AudioManager.Instance?.PlayReload();
            }
        }
        else
        {
            if (_isReloading[playerIndex] && !_triggerFiredOnEmpty[playerIndex])
            {
                _reloadTimer[playerIndex] = 0f;
                _isReloading[playerIndex] = false;
            }
        }
    }

    void HandleShooting(int playerIndex, GunInputReader.PlayerInput input)
    {
        if (_isReloading[playerIndex]) return;
        if (input.trackingLost) return;

        bool triggerDown = input.fire && !_triggerHeld[playerIndex];
        _triggerHeld[playerIndex] = input.fire;

        if (!triggerDown) return;

        if (_ammo[playerIndex] <= 0)
        {
            AudioManager.Instance?.PlayEmptyClick();
            _triggerFiredOnEmpty[playerIndex] = true; // ← trigger reload
            return;
        }

        TryShoot(playerIndex, input.aimPosition);
    }

    void TryShoot(int playerIndex, Vector2 normalisedAim)
    {
        _ammo[playerIndex]--;
        UpdateAmmoUI();
        AudioManager.Instance?.PlayGunshot();

        Vector2 centerScreen = new Vector2(
            normalisedAim.x * Screen.width,
            normalisedAim.y * Screen.height
        );

        // Cast multiple rays in a pattern around the crosshair center
        Vector2[] offsets = new Vector2[]
        {
        Vector2.zero,                                          // center
        new Vector2(hitTolerancePixels, 0),                   // right
        new Vector2(-hitTolerancePixels, 0),                  // left
        new Vector2(0, hitTolerancePixels),                   // up
        new Vector2(0, -hitTolerancePixels),                  // down
        new Vector2(hitTolerancePixels, hitTolerancePixels),  // top right
        new Vector2(-hitTolerancePixels, hitTolerancePixels), // top left
        new Vector2(hitTolerancePixels, -hitTolerancePixels), // bottom right
        new Vector2(-hitTolerancePixels, -hitTolerancePixels) // bottom left
        };

        foreach (Vector2 offset in offsets)
        {
            Vector3 screenPos = new Vector3(
                centerScreen.x + offset.x,
                centerScreen.y + offset.y,
                0f
            );

            Ray ray = Camera.main.ScreenPointToRay(screenPos);

            if (Physics.Raycast(ray, out RaycastHit hit, raycastDistance, enemyLayer))
            {
                Enemy enemy = hit.collider.GetComponent<Enemy>();
                if (enemy != null && !enemy.isDead)
                {
                    enemy.GetHit(playerIndex);
                    return; // stop after first hit — one shot, one hit
                }

                StageTarget stageTarget = hit.collider.GetComponent<StageTarget>();
                if (stageTarget != null)
                {
                    stageTarget.OnShot();
                    return;
                }
            }
        }
    }

    void UpdateAmmoUI()
    {
        HUDManager.Instance?.UpdateAmmo(_ammo[0], _ammo[1], maxAmmo);
    }

    public bool IsReloading(int playerIndex) => _isReloading[playerIndex];
    public int GetAmmo(int playerIndex) => _ammo[playerIndex];
    public float GetReloadProgress(int playerIndex) => _reloadTimer[playerIndex] / reloadTime;
}