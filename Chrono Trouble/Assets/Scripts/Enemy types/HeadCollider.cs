using UnityEngine;

public class HeadCollider : MonoBehaviour
{
    public Enemy parentEnemy;
    public int headshotDamage = 3;

    public void GetHit(int playerIndex)
    {
        if (parentEnemy != null && !parentEnemy.isDead)
            parentEnemy.GetHit(playerIndex, headshotDamage);
    }
}