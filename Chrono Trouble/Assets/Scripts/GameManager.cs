using UnityEngine;
using TMPro;

public class GameManager : MonoBehaviour
{
    public static GameManager Instance;

    [Header("Lives")]
    public int startingLives = 3;
    private int[] _lives = new int[2];
    private int[] _scores = new int[2];

    [Header("UI")]
    public GameObject gameOverPanel;
    public GameObject missionCompletePanel;
    public TextMeshProUGUI gameOverText;

    void Awake()
    {
        if (Instance != null) { Destroy(gameObject); return; }
        Instance = this;
        _lives[0] = startingLives;
        _lives[1] = startingLives;
        if (gameOverPanel) gameOverPanel.SetActive(false);
        if (missionCompletePanel) missionCompletePanel.SetActive(false);
        UpdateHUD();
    }

    public void EnemyShot(int playerIndex)
    {
        if (!GunInputReader.Instance.players[playerIndex].isConnected) return;

        _lives[playerIndex]--;
        if (_lives[playerIndex] < 0) _lives[playerIndex] = 0;

        UpdateHUD();
        CheckGameOver();
    }

    public void AddScore(int points, int playerIndex)
    {
        _scores[playerIndex] += points;
        UpdateHUD();
    }

    public void OnTimerExpired()
    {
        // Called by StageTimer when time runs out
        bool p1Connected = GunInputReader.Instance.players[0].isConnected;
        bool p2Connected = GunInputReader.Instance.players[1].isConnected;

        bool anyoneAlive = (_lives[0] > 0 && p1Connected) ||
                           (_lives[1] > 0 && p2Connected);

        if (anyoneAlive)
        {
            // At least one player survived — mission complete
            if (missionCompletePanel)
                missionCompletePanel.SetActive(true);
        }
        else
        {
            // Everyone is dead — game over
            if (gameOverPanel)
                gameOverPanel.SetActive(true);
        }
    }

    void UpdateHUD()
    {
        HUDManager.Instance?.UpdateLives(_lives[0], _lives[1]);
        HUDManager.Instance?.UpdateScore(_scores[0], _scores[1]);
    }

    void CheckGameOver()
    {
        bool p1Connected = GunInputReader.Instance.players[0].isConnected;
        bool p2Connected = GunInputReader.Instance.players[1].isConnected;

        // Game over only if ALL connected players are dead
        bool p1Dead = _lives[0] <= 0 && p1Connected;
        bool p2Dead = _lives[1] <= 0 && p2Connected;

        bool gameOver = false;

        if (p1Connected && p2Connected)
            gameOver = p1Dead && p2Dead; // both must be dead
        else if (p1Connected)
            gameOver = p1Dead;
        else if (p2Connected)
            gameOver = p2Dead;

        if (gameOver)
        {
            StageTimer.Instance?.StopTimer();
            if (gameOverPanel)
                gameOverPanel.SetActive(true);
        }
    }
}