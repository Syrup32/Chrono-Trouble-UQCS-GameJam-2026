using UnityEngine;

public class BirdHitProxy : MonoBehaviour
{
    public BirdSwarm parentSwarm;

    public void GetHit(int playerIndex, int damage = 1)
    {
        if (parentSwarm != null && !parentSwarm.isDead)
            parentSwarm.GetHit(playerIndex, damage);
    }
}