using UnityEngine;
using TMPro;
using System.Collections;

public class StageTimer : MonoBehaviour
{
    public static StageTimer Instance;

    [Header("Settings")]
    public float stageDuration = 90f;

    [Header("UI")]
    public TextMeshProUGUI timerText;

    private float _timeRemaining;
    private bool _timerRunning = false;

    void Awake()
    {
        if (Instance != null) { Destroy(gameObject); return; }
        Instance = this;
    }

    void Start()
    {
        _timeRemaining = stageDuration;
        _timerRunning = true;
        UpdateTimerDisplay();
    }

    void Update()
    {
        if (!_timerRunning) return;

        _timeRemaining -= Time.deltaTime;
        UpdateTimerDisplay();

        if (_timeRemaining <= 0f)
        {
            _timeRemaining = 0f;
            _timerRunning = false;
            UpdateTimerDisplay();
            StartCoroutine(EndStage());
        }
    }

    void UpdateTimerDisplay()
    {
        if (timerText == null) return;

        // Display as SS.mm (seconds and milliseconds to 2 decimal places)
        timerText.text = _timeRemaining.ToString("F2");

        // Turn red when under 10 seconds
        timerText.color = _timeRemaining <= 10f ? Color.red : Color.white;
    }

    IEnumerator EndStage()
    {
        // Notify GameManager that time is up
        GameManager.Instance?.OnTimerExpired();

        // Brief pause before transitioning
        yield return new WaitForSeconds(3f);
        GameFlowManager.Instance?.GoToStageSelect();
    }

    public void StopTimer()
    {
        _timerRunning = false;
    }

    public float GetTimeRemaining() => _timeRemaining;
}