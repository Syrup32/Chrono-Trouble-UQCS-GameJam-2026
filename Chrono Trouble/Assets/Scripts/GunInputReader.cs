using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.Controls;
using UnityEngine.InputSystem.LowLevel;

public class GunInputReader : MonoBehaviour
{
    public static GunInputReader Instance;

    [System.Serializable]
    public struct PlayerInput
    {
        public Vector2 aimPosition;
        public bool fire;
        public bool trackingLost;
        public bool isConnected;
        public bool isUsingMouse;
    }

    public PlayerInput[] players = new PlayerInput[2];

    void Awake()
    {
        if (Instance != null)
        {
            Destroy(gameObject);
            return;
        }
        Instance = this;
        Cursor.visible = false;
        Cursor.lockState = CursorLockMode.Confined;
    }

    void Update()
    {
        int gunCount = Joystick.all.Count;

        if (gunCount >= 2)
        {
            players[0] = ReadGun(Joystick.all[0], 0);
            players[1] = ReadGun(Joystick.all[1], 1);
        }
        else if (gunCount == 1)
        {
            players[0] = ReadGun(Joystick.all[0], 0);
            players[1] = ReadMouse(1);
        }
        else
        {
            players[0] = ReadMouse(0);
            players[1] = new PlayerInput { isConnected = false };
        }
    }

    PlayerInput ReadGun(Joystick gun, int playerIndex)
    {
        var triggerControl = gun.TryGetChildControl<ButtonControl>("trigger");
        var button2Control = gun.TryGetChildControl<ButtonControl>("button2");

        float normX = 0.5f;
        float normY = 0.5f;

        try
        {
            IRGunState state = default;
            gun.CopyState(out state);  // removed InputState.Change line

            short rawX = state.axisX;
            short rawY = state.axisY;

            normX = (rawX + 32767f) / 65534f;
            normY = 1f - (rawY + 32767f) / 65534f;
        }
        catch (System.Exception e)
        {
            Debug.LogWarning($"State read failed: {e.Message}");
        }

        bool trackingLost = button2Control != null && button2Control.isPressed;

        return new PlayerInput
        {
            aimPosition = new Vector2(normX, normY),
            fire = triggerControl != null && triggerControl.isPressed,
            trackingLost = trackingLost,
            isConnected = true,
            isUsingMouse = false
        };
    }

    PlayerInput ReadMouse(int playerIndex)
    {
        if (Mouse.current == null)
            return new PlayerInput { isConnected = false };

        Vector2 mousePos = Mouse.current.position.ReadValue();

        return new PlayerInput
        {
            aimPosition = new Vector2(
                mousePos.x / Screen.width,
                mousePos.y / Screen.height
            ),
            fire = Mouse.current.leftButton.isPressed,
            trackingLost = Mouse.current.rightButton.isPressed,
            isConnected = true,
            isUsingMouse = true
        };
    }
}