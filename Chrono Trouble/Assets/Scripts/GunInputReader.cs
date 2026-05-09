using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.Controls;

public class GunInputReader : MonoBehaviour
{
    public static GunInputReader Instance;

    [System.Serializable]
    public struct PlayerInput
    {
        public Vector2 aimPosition;  // screen space 0-1
        public bool fire;
        public bool trackingLost;
        public bool isConnected;
    }

    public PlayerInput[] players = new PlayerInput[2];

    void Awake()
    {
        Instance = this;
        Cursor.visible = false;
        Cursor.lockState = CursorLockMode.Confined;
    }

    void Update()
    {
        int gunCount = Joystick.all.Count;

        for (int i = 0; i < 2; i++)
        {
            // Try to get a physical gun for this player slot
            if (i < gunCount)
            {
                players[i] = ReadGun(Joystick.all[i], i);
            }
            else
            {
                // Fall back to mouse for this slot
                players[i] = ReadMouse(i);
            }
        }
    }

    PlayerInput ReadGun(Joystick gun, int playerIndex)
    {
        var stick = gun.TryGetChildControl<StickControl>("stick");
        var triggerControl = gun.TryGetChildControl<ButtonControl>("trigger");
        var button2Control = gun.TryGetChildControl<ButtonControl>("button2");

        float normX = 0.5f;
        float normY = 0.5f;

        if (stick != null)
        {
            normX = (stick.x.ReadValue() + 1f) / 2f;
            normY = 1f - (stick.y.ReadValue() + 1f) / 2f;
        }

        bool trackingLost = button2Control != null && button2Control.isPressed;

        return new PlayerInput
        {
            aimPosition = new Vector2(normX, normY),
            fire = triggerControl != null && triggerControl.isPressed,
            trackingLost = trackingLost,
            isConnected = true
        };
    }

    PlayerInput ReadMouse(int playerIndex)
    {
        if (playerIndex > 0)
        {
            return new PlayerInput { isConnected = false };
        }

        Vector2 mousePos = Mouse.current.position.ReadValue();

        return new PlayerInput
        {
            aimPosition = new Vector2(
                mousePos.x / Screen.width,
                mousePos.y / Screen.height
            ),
            fire = Mouse.current.leftButton.isPressed,
            trackingLost = Mouse.current.rightButton.isPressed,
            isConnected = true
        };
    }
}