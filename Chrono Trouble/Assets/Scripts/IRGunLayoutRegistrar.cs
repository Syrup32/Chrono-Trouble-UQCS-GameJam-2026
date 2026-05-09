using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.Layouts;
using UnityEngine.InputSystem.LowLevel;
using UnityEngine.InputSystem.Utilities;

#if UNITY_EDITOR
using UnityEditor;
#endif

[InitializeOnLoad]
public static class IRGunLayoutRegistrar
{
    static IRGunLayoutRegistrar()
    {
        RegisterLayout();
    }

    [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
    static void RegisterLayout()
    {
        InputSystem.RegisterLayout<IRGunDevice>(
            matches: new InputDeviceMatcher()
                .WithInterface("HID")
                .WithCapability("vendorId", 0xE502)
                .WithCapability("productId", 0xBBAB)
        );

#if UNITY_EDITOR
        // Remove any stale saved device states after domain reload
        // to prevent duplicate device ID errors
        InputSystem.FlushDisconnectedDevices();
#endif
    }
}

[InputControlLayout(stateType = typeof(IRGunState))]
public class IRGunDevice : Joystick { }

[System.Runtime.InteropServices.StructLayout(
    System.Runtime.InteropServices.LayoutKind.Explicit, Size = 6)]
public struct IRGunState : IInputStateTypeInfo
{
    public FourCC format => new FourCC('H', 'I', 'D');

    [System.Runtime.InteropServices.FieldOffset(1)]
    [InputControl(name = "trigger", bit = 0, layout = "Button")]
    [InputControl(name = "button2", bit = 1, layout = "Button")]
    public byte buttons;

    [System.Runtime.InteropServices.FieldOffset(2)]
    [InputControl(name = "stick/x", format = "SHRT", layout = "Axis",
        parameters = "normalize,normalizeMin=-1,normalizeMax=1,normalizeZero=0")]
    public short axisX;

    [System.Runtime.InteropServices.FieldOffset(4)]
    [InputControl(name = "stick/y", format = "SHRT", layout = "Axis",
        parameters = "normalize,normalizeMin=-1,normalizeMax=1,normalizeZero=0,invert")]
    public short axisY;
}