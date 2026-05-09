using UnityEngine;

public class StageTarget : MonoBehaviour
{
    public int stageIndex;
    public string stageName;

    void Start()
    {
        // Make sure this object is on the Enemy layer
        // so the ShootingSystem raycast can detect it
        gameObject.layer = LayerMask.NameToLayer("Enemy");
    }

    public void OnShot()
    {
        GameFlowManager.Instance?.GoToStage(stageIndex);
    }
}