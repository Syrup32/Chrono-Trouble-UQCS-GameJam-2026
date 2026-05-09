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
    public TextMeshProUGUI livesP1Text;
    public TextMeshProUGUI livesP2Text;
    public TextMeshProUGUI scoreP1Text;
    public TextMeshProUGUI scoreP2Text;
    public GameObject gameOverPanel;

    void Awake()
    {
        Instance = this;
        _lives[0] = startingLives;
        _lives[1] = startingLives;
        if (gameOverPanel) gameOverPanel.SetActive(false);
        UpdateUI();
    }

    public void EnemyShot()
    {
        // Enemy shoots both players
        for (int i = 0; i < 2; i++)
        {
            if (!GunInputReader.Instance.players[i].isConnected) continue;
            _lives[i]--;
            if (_lives[i] < 0) _lives[i] = 0;
        }

        UpdateUI();
        CheckGameOver();
    }

    public void AddScore(int points, int playerIndex)
    {
        _scores[playerIndex] += points;
        UpdateUI();
    }

    void UpdateUI()
    {
        if (livesP1Text) livesP1Text.text = $"P1 Lives: {_lives[0]}";
        if (livesP2Text) livesP2Text.text = $"P2 Lives: {_lives[1]}";
        if (scoreP1Text) scoreP1Text.text = $"P1 Score: {_scores[0]}";
        if (scoreP2Text) scoreP2Text.text = $"P2 Score: {_scores[1]}";
    }

    void CheckGameOver()
    {
        bool p1Connected = GunInputReader.Instance.players[0].isConnected;
        bool p2Connected = GunInputReader.Instance.players[1].isConnected;

        bool gameOver = (_lives[0] <= 0 && p1Connected) ||
                        (_lives[1] <= 0 && p2Connected);

        if (gameOver && gameOverPanel)
            gameOverPanel.SetActive(true);
    }
}