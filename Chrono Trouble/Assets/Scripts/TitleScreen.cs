using UnityEngine;
using UnityEngine.InputSystem;
using TMPro;

public class TitleScreen : MonoBehaviour
{
    public AudioClip titleVO;
    public AudioSource audioSource;

    private bool _waitingForRelease = true;

    void Start()
    {
        if (audioSource && titleVO)
            audioSource.PlayOneShot(titleVO);
    }

    void Update()
    {
        if (GunInputReader.Instance == null) return;

        bool anyTrigger = GunInputReader.Instance.players[0].fire ||
                          GunInputReader.Instance.players[1].fire;

        if (_waitingForRelease)
        {
            if (!anyTrigger) _waitingForRelease = false;
            return;
        }

        if (anyTrigger)
        {
            GameFlowManager.Instance?.GoToTutorial();
        }
    }
}