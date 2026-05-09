using UnityEngine;

public class CrosshairController : MonoBehaviour
{
    public RectTransform crosshairP1;
    public RectTransform crosshairP2;

    void Update()
    {
        if (GunInputReader.Instance == null) return;

        UpdateCrosshair(crosshairP1, GunInputReader.Instance.players[0], 0);
        UpdateCrosshair(crosshairP2, GunInputReader.Instance.players[1], 1);
    }

    void UpdateCrosshair(RectTransform crosshair, GunInputReader.PlayerInput input, int playerIndex)
    {
        if (!input.isConnected || input.trackingLost)
        {
            crosshair.gameObject.SetActive(false);
            return;
        }

        crosshair.gameObject.SetActive(true);

        crosshair.position = new Vector2(
            input.aimPosition.x * Screen.width,
            input.aimPosition.y * Screen.height
        );
    }
}