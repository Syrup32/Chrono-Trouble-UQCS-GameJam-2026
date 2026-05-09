using UnityEngine;
using TMPro;

public class StageSelectManager : MonoBehaviour
{
    [Header("UI")]
    public TextMeshProUGUI instructionText;

    void Start()
    {
        if (instructionText)
            instructionText.text = "Shoot a stage to begin!";
    }
}