using UnityEngine;

public class TitleScreen : MonoBehaviour
{
    private bool _waitingForRelease = true;

    void Update()
    {
        if (GunInputReader.Instance == null) return;

        // Wait for trigger to be released first to avoid
        // immediately skipping if trigger was held on load
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