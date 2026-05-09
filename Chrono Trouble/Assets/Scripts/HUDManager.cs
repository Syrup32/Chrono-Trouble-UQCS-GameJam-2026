using UnityEngine;
using TMPro;

public class HUDManager : MonoBehaviour
{
    public static HUDManager Instance;

    [Header("Lives")]
    public TextMeshProUGUI livesP1Text;
    public TextMeshProUGUI livesP2Text;

    [Header("Score")]
    public TextMeshProUGUI scoreP1Text;
    public TextMeshProUGUI scoreP2Text;

    [Header("Ammo")]
    public TextMeshProUGUI ammoP1Text;
    public TextMeshProUGUI ammoP2Text;

    void Awake()
    {
        if (Instance != null) { Destroy(gameObject); return; }
        Instance = this;
    }

    public void UpdateLives(int p1Lives, int p2Lives)
    {
        if (livesP1Text) livesP1Text.text = $"P1 Lives: {p1Lives}";
        if (livesP2Text) livesP2Text.text = $"P2 Lives: {p2Lives}";
    }

    public void UpdateScore(int p1Score, int p2Score)
    {
        if (scoreP1Text) scoreP1Text.text = $"P1 Score: {p1Score}";
        if (scoreP2Text) scoreP2Text.text = $"P2 Score: {p2Score}";
    }

    public void UpdateAmmo(int p1Ammo, int p2Ammo, int maxAmmo)
    {
        if (ammoP1Text) ammoP1Text.text = $"P1 Ammo: {p1Ammo}/{maxAmmo}";
        if (ammoP2Text) ammoP2Text.text = $"P2 Ammo: {p2Ammo}/{maxAmmo}";
    }
}