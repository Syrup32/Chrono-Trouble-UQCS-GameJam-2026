using UnityEngine;
using UnityEngine.InputSystem;

public class GunInputReader : MonoBehaviour
{
    void Update()
    {
        for (int i = 0; i < 2; i++)
        {
            if (i >= Gamepad.all.Count) continue;

            Gamepad gun = Gamepad.all[i];

            float rawX = gun.leftStick.x.ReadValue();
            float rawY = gun.leftStick.y.ReadValue();

            float normX = (rawX + 1f) / 2f;
            float normY = 1f - (rawY + 1f) / 2f; // flip Y

            bool trigger = gun.buttonSouth.isPressed;
            bool trackingLost = gun.buttonEast.isPressed;

            Debug.Log($"P{i + 1} | X:{normX:F2} Y:{normY:F2} | Fire:{trigger} | TrackingLost:{trackingLost}");
        }
    }
}