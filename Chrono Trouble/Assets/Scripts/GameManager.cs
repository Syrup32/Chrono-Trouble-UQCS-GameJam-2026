using UnityEngine;

public class GameManager : MonoBehaviour
{
    public static GameManager Instance;

    [Header("Lives")]
    public int startingLives = 3;
    private int[] _lives = new int[2];
    private int[] _scores = new int[2];

    [Header("Game Over")]
    public GameObject gameOverPanel;

    void Awake()
    {
        if (Instance != null) { Destroy(gameObject); return; }
        Instance = this;
        _lives[0] = startingLives;
        _lives[1] = startingLives;
        if (gameOverPanel) gameOverPanel.SetActive(false);
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

    void UpdateHUD()
    {
        HUDManager.Instance?.UpdateLives(_lives[0], _lives[1]);
        HUDManager.Instance?.UpdateScore(_scores[0], _scores[1]);
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