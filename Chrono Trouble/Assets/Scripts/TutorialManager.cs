using UnityEngine;
using UnityEngine.UI;
using TMPro;
using System.Collections;

public class TutorialManager : MonoBehaviour
{
    [Header("VO Clips")]
    public AudioClip voIntro;
    public AudioClip voTrigger;
    public AudioClip voReload;
    public AudioClip voTask;

    [Header("UI")]
    public GameObject popupPanel;
    public TextMeshProUGUI popupText;
    public CanvasGroup popupCanvasGroup;
    public TextMeshProUGUI timerText;

    [Header("Targets")]
    public GameObject targetPrefab;
    public Transform[] targetSpawnPoints;

    [Header("Settings")]
    public float taskDuration = 15f;
    public float popupFadeDuration = 0.5f;

    private AudioSource _audioSource;
    private bool _waitingForDismiss = false;
    private bool _dismissed = false;
    private bool _triggerWasPressed = false;
    private float _taskTimer = 0f;
    private bool _taskRunning = false;
    private int _shotsHit = 0;
    private bool _hasShot = false;

    string[] _popupTexts = new string[]
    {
        "This is the only training and test we give to people who want to become mercenaries.\n\nAll mercenaries are issued a Mk23 SOCOM, use it wisely.\n\nThat's all. Good luck.\n\n[Pull trigger to continue]",
        "Aim the gun at the screen and you will see your crosshair.\n\nPull the trigger to fire your gun.\n\n[Pull trigger to continue]",
        "Run out of ammunition or aim off of the screen to reload.\n\n[Pull trigger to continue]",
        "You are always under a time limit.\n\nFor this training, just take out as many targets as possible in 15 seconds.\n\n[Pull trigger to continue]"
    };

    void Awake()
    {
        _audioSource = gameObject.AddComponent<AudioSource>();
    }

    void Start()
    {
        if (timerText) timerText.gameObject.SetActive(false);
        if (popupPanel) popupPanel.SetActive(false);
        StartCoroutine(RunTutorial());
    }

    void Update()
    {
        bool anyTrigger = GunInputReader.Instance != null &&
                          (GunInputReader.Instance.players[0].fire ||
                           GunInputReader.Instance.players[1].fire);

        if (anyTrigger && !_triggerWasPressed)
        {
            if (_waitingForDismiss)
            {
                _dismissed = true;
                // Stop VO when player dismisses
                if (_audioSource.isPlaying)
                    _audioSource.Stop();
            }
        }
        _triggerWasPressed = anyTrigger;

        if (_taskRunning)
        {
            _taskTimer -= Time.deltaTime;
            UpdateTimerDisplay();

            if (_taskTimer <= 0f)
            {
                _taskTimer = 0f;
                _taskRunning = false;
                UpdateTimerDisplay();
                StartCoroutine(EndTask());
            }
        }

        if (!_hasShot && GunInputReader.Instance != null)
        {
            bool anyFire = GunInputReader.Instance.players[0].fire ||
                           GunInputReader.Instance.players[1].fire;
            if (anyFire) _hasShot = true;
        }
    }

    IEnumerator RunTutorial()
    {
        // Step 0 — Intro
        yield return ShowPopup(0, voIntro);

        // Step 1 — Trigger tutorial
        yield return ShowPopup(1, voTrigger);

        // Wait for player to fire at least once
        _hasShot = false;
        yield return new WaitUntil(() => _hasShot);

        // Step 2 — Reload tutorial
        yield return ShowPopup(2, voReload);

        // Wait for player to reload
        yield return new WaitUntil(() =>
            ShootingSystem.Instance != null &&
            (ShootingSystem.Instance.IsReloading(0) ||
             ShootingSystem.Instance.IsReloading(1)));

        yield return new WaitUntil(() =>
            ShootingSystem.Instance != null &&
            (ShootingSystem.Instance.GetAmmo(0) == 10 ||
             ShootingSystem.Instance.GetAmmo(1) == 10));

        // Step 3 — Task tutorial
        yield return ShowPopup(3, voTask);

        // Start task phase
        _taskTimer = taskDuration;
        _taskRunning = true;

        if (timerText)
        {
            timerText.gameObject.SetActive(true);
            UpdateTimerDisplay();
        }

        StartCoroutine(SpawnTargets());
    }

    IEnumerator ShowPopup(int textIndex, AudioClip voClip)
    {
        _dismissed = false;
        _waitingForDismiss = false;

        if (popupPanel) popupPanel.SetActive(true);
        if (popupText) popupText.text = _popupTexts[textIndex];
        if (popupCanvasGroup) popupCanvasGroup.alpha = 1f;

        // Play VO
        if (voClip != null)
            _audioSource.PlayOneShot(voClip);

        // Brief delay before allowing dismissal
        yield return new WaitForSeconds(0.5f);
        _waitingForDismiss = true;

        // Wait for trigger press to dismiss
        yield return new WaitUntil(() => _dismissed);

        // Fade out popup
        yield return FadeOutPopup();

        yield return new WaitForSeconds(0.3f);
    }

    IEnumerator FadeOutPopup()
    {
        if (popupCanvasGroup == null)
        {
            if (popupPanel) popupPanel.SetActive(false);
            yield break;
        }

        float elapsed = 0f;
        while (elapsed < popupFadeDuration)
        {
            elapsed += Time.deltaTime;
            popupCanvasGroup.alpha = 1f - (elapsed / popupFadeDuration);
            yield return null;
        }

        popupCanvasGroup.alpha = 0f;
        if (popupPanel) popupPanel.SetActive(false);
    }

    IEnumerator SpawnTargets()
    {
        while (_taskRunning)
        {
            if (targetSpawnPoints.Length > 0)
            {
                Transform spawnPoint = targetSpawnPoints[
                    Random.Range(0, targetSpawnPoints.Length)];
                GameObject target = Instantiate(
                    targetPrefab, spawnPoint.position, spawnPoint.rotation);

                Enemy enemy = target.GetComponent<Enemy>();
                if (enemy != null)
                {
                    enemy.timeBeforeAttacking = 999f;
                    enemy.canAttack = false;
                    enemy.OnEnemyDeactivated += () => _shotsHit++;
                }
            }

            yield return new WaitForSeconds(0.4f);
        }
    }

    IEnumerator EndTask()
    {
        yield return new WaitForSeconds(1f);
        if (timerText) timerText.gameObject.SetActive(false);
        GameFlowManager.Instance?.GoToStageSelect();
    }

    void UpdateTimerDisplay()
    {
        if (timerText == null) return;

        // Display as float with 2 decimal places
        timerText.text = _taskTimer.ToString("F2");

        // Turn red when under 5 seconds
        timerText.color = _taskTimer <= 5f ? Color.red : Color.white;
    }
}