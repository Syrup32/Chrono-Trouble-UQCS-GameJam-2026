using UnityEngine;
using TMPro;

public class StageSelectManager : MonoBehaviour
{
    [Header("UI")]
    public TextMeshProUGUI instructionText;

    [Header("Audio")]
    public AudioClip stageSelectVO;
    private AudioSource _audioSource;

    void Start()
    {
        _audioSource = gameObject.AddComponent<AudioSource>();

        if (instructionText)
            instructionText.text = "Shoot a stage to begin!";

        if (stageSelectVO != null)
            _audioSource.PlayOneShot(stageSelectVO);
    }
}