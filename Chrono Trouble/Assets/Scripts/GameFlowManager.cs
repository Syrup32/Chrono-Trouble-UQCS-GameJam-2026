using UnityEngine;
using UnityEngine.SceneManagement;

public class GameFlowManager : MonoBehaviour
{
    public static GameFlowManager Instance;

    public int selectedStage = -1;

    public const int SCENE_TITLE = 0;
    public const int SCENE_TUTORIAL = 1;
    public const int SCENE_STAGE_SELECT = 2;
    public const int SCENE_STAGE_1 = 3;
    public const int SCENE_STAGE_2 = 4;
    public const int SCENE_STAGE_3 = 5;

    void Awake()
    {
        if (Instance != null)
        {
            Destroy(gameObject);
            return;
        }
        Instance = this;
        DontDestroyOnLoad(gameObject);
    }

    public void GoToTutorial()
    {
        SceneTransition.Instance?.LoadScene(SCENE_TUTORIAL);
    }

    public void GoToStageSelect()
    {
        SceneTransition.Instance?.LoadScene(SCENE_STAGE_SELECT);
    }

    public void GoToStage(int stageIndex)
    {
        selectedStage = stageIndex;
        SceneTransition.Instance?.LoadScene(SCENE_STAGE_1 + stageIndex);
    }

    public void GoToTitle()
    {
        SceneTransition.Instance?.LoadScene(SCENE_TITLE);
    }
}