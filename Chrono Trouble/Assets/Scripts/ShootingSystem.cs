using UnityEngine;
using TMPro;
using System.Collections.Generic;

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

        Vector2[] offsets = new Vector2[]
        {
        Vector2.zero,
        new Vector2(hitTolerancePixels, 0),
        new Vector2(-hitTolerancePixels, 0),
        new Vector2(0, hitTolerancePixels),
        new Vector2(0, -hitTolerancePixels),
        new Vector2(hitTolerancePixels, hitTolerancePixels),
        new Vector2(-hitTolerancePixels, hitTolerancePixels),
        new Vector2(hitTolerancePixels, -hitTolerancePixels),
        new Vector2(-hitTolerancePixels, -hitTolerancePixels)
        };

        // Collect all unique hits across all offset rays
        // Key = collider, Value = screen distance from center
        Dictionary<Collider, float> hitColliders =
            new Dictionary<Collider, float>();

        foreach (Vector2 offset in offsets)
        {
            Vector3 screenPos = new Vector3(
                centerScreen.x + offset.x,
                centerScreen.y + offset.y,
                0f
            );

            Ray ray = Camera.main.ScreenPointToRay(screenPos);
            RaycastHit[] hits = Physics.RaycastAll(
                ray, raycastDistance, enemyLayer);

            foreach (RaycastHit hit in hits)
            {
                if (hitColliders.ContainsKey(hit.collider)) continue;

                // Measure how far this hit point is from crosshair center in screen space
                Vector3 hitScreenPos = Camera.main.WorldToScreenPoint(hit.point);
                float screenDist = Vector2.Distance(
                    new Vector2(hitScreenPos.x, hitScreenPos.y), centerScreen);

                hitColliders[hit.collider] = screenDist;
            }
        }

        if (hitColliders.Count == 0) return;

        // Find the collider closest to crosshair center
        Collider closest = null;
        float closestDist = float.MaxValue;

        foreach (var kvp in hitColliders)
        {
            if (kvp.Value < closestDist)
            {
                closestDist = kvp.Value;
                closest = kvp.Key;
            }
        }

        if (closest == null) return;

        // Process the closest hit
        BirdHitProxy birdProxy = closest.GetComponent<BirdHitProxy>();
        if (birdProxy != null)
        {
            birdProxy.GetHit(playerIndex, 1);
            return;
        }

        Enemy enemy = closest.GetComponent<Enemy>();
        if (enemy != null && !enemy.isDead)
        {
            enemy.GetHit(playerIndex, 1);
            return;
        }

        StageTarget stageTarget = closest.GetComponent<StageTarget>();
        if (stageTarget != null)
        {
            stageTarget.OnShot();
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