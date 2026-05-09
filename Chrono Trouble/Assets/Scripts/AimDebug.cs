using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.Controls;

public class InputDiagnostic : MonoBehaviour
{
    void Update()
    {
        if (Joystick.all.Count == 0) return;

        var gun = Joystick.all[0];

        // Print all available controls and their values
        foreach (var control in gun.allControls)
        {
            if (control is AxisControl axis)
            {
                float val = axis.ReadValue();
                if (Mathf.Abs(val) > 0.01f)
                {
                    Debug.Log($"Control: {control.path} = {val:F3}");
                }
            }
        }
    }
}