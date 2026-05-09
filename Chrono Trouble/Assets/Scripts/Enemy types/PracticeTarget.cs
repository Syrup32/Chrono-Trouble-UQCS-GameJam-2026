using UnityEngine;

public class PracticeTarget : Enemy
{
    protected override void Awake()
    {
        base.Awake();
        canAttack = false;
        maxHitPoints = 1;
    }
}