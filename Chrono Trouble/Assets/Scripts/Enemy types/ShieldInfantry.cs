using UnityEngine;

public class ShieldInfantry : InfantryEnemy
{
    protected override void Awake()
    {
        base.Awake();
        maxHitPoints = 1;
        // Shield logic will be added later when models are in
    }
}