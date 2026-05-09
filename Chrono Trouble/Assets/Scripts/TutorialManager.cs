using UnityEngine;
using UnityEngine.UI;
using TMPro;
using System.Collections;

public class TutorialManager : MonoBehaviour
{
    [Header("UI")]
    public TextMeshProUGUI instructionText;
    public TextMeshProUGUI progressText;

    [Header("Targets")]
    public GameObject targetPrefab;
    public Transform[] targetSpawnPoints;

    private int _currentStep = 0;
    private bool _stepComplete = false;
    private int _shotsHit = 0;
    private int _shotsFired = 0;
    private bool _hasReloaded = false;

    string[] _instructions = new string[]
    {
        "Welcome! Point your gun at the screen.\nYour crosshair will appear when tracking.",
        "Pull the trigger to shoot.\nHit 3 targets to continue.",
        "You have 10 rounds per magazine.\nEmpty your gun to trigger an auto-reload.\nOr point off screen to reload manually.",
        "Enemies will shoot back if not eliminated in time.\nShoot the target before time runs out!",
        "Tutorial complete!\nPull trigger to continue to Stage Select."
    };

    void Start()
    {
        StartCoroutine(RunTutorial());
    }

    IEnumerator RunTutorial()
    {
        // Step 0 — tracking
        yield return RunStep0();

        // Step 1 — basic shooting
        yield return RunStep1();

        // Step 2 — reload
        yield return RunStep2();

        // Step 3 — timed enemy
        yield return RunStep3();

        // Step 4 — complete
        yield return RunStep4();

        GameFlowManager.Instance?.GoToStageSelect();
    }

    IEnumerator RunStep0()
    {
        instructionText.text = _instructions[0];
        progressText.text = "";

        // Wait until at least one player has tracking
        yield return new WaitUntil(() =>
            GunInputReader.Instance != null &&
            (!GunInputReader.Instance.players[0].trackingLost ||
             !GunInputReader.Instance.players[1].trackingLost)
        );

        yield return new WaitForSeconds(1f);
    }

    IEnumerator RunStep1()
    {
        instructionText.text = _instructions[1];
        _shotsHit = 0;

        // Spawn 3 targets one at a time
        for (int i = 0; i < 3; i++)
        {
            progressText.text = $"Targets hit: {_shotsHit}/3";
            SpawnTarget();
            yield return new WaitUntil(() => _shotsHit > i);
        }

        progressText.text = "Targets hit: 3/3";
        yield return new WaitForSeconds(1f);
    }

    IEnumerator RunStep2()
    {
        instructionText.text = _instructions[2];
        progressText.text = "Waiting for reload...";
        _hasReloaded = false;

        yield return new WaitUntil(() =>
        {
            if (ShootingSystem.Instance == null) return false;
            return ShootingSystem.Instance.GetAmmo(0) == 0 ||
                   ShootingSystem.Instance.GetAmmo(1) == 0 ||
                   ShootingSystem.Instance.IsReloading(0) ||
                   ShootingSystem.Instance.IsReloading(1);
        });

        progressText.text = "Reloading...";

        yield return new WaitUntil(() =>
        {
            if (ShootingSystem.Instance == null) return false;
            return ShootingSystem.Instance.GetAmmo(0) == 10 ||
                   ShootingSystem.Instance.GetAmmo(1) == 10;
        });

        progressText.text = "Reloaded!";
        yield return new WaitForSeconds(1f);
    }

    IEnumerator RunStep3()
    {
        instructionText.text = _instructions[3];
        progressText.text = "Shoot the target!";

        _shotsHit = 0;
        GameObject target = SpawnTarget(useTimer: true);

        yield return new WaitUntil(() => _shotsHit > 0 || target == null || !target.activeSelf);

        yield return new WaitForSeconds(1f);
    }

    IEnumerator RunStep4()
    {
        instructionText.text = _instructions[4];
        progressText.text = "";

        bool _waitingForRelease = true;
        yield return new WaitUntil(() =>
        {
            if (GunInputReader.Instance == null) return false;
            bool anyTrigger = GunInputReader.Instance.players[0].fire ||
                              GunInputReader.Instance.players[1].fire;
            if (_waitingForRelease) { if (!anyTrigger) _waitingForRelease = false; return false; }
            return anyTrigger;
        });
    }

    GameObject SpawnTarget(bool useTimer = false)
    {
        if (targetSpawnPoints.Length == 0) return null;

        Transform spawnPoint = targetSpawnPoints[
            Random.Range(0, targetSpawnPoints.Length)];

        GameObject target = Instantiate(targetPrefab,
            spawnPoint.position, spawnPoint.rotation);

        Enemy enemy = target.GetComponent<Enemy>();
        if (enemy != null)
        {
            enemy.timeToShoot = useTimer ? 3f : 999f;
            enemy.OnEnemyDeactivated += () => _shotsHit++;
        }

        return target;
    }
}