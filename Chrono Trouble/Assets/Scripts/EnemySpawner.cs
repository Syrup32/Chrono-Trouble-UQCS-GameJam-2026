using UnityEngine;
using System.Collections;

public class EnemySpawner : MonoBehaviour
{
    [Header("Spawning")]
    public GameObject enemyPrefab;
    public Transform[] spawnPoints;
    public float spawnInterval = 2f;
    public int maxEnemiesAtOnce = 3;

    private int _activeEnemies = 0;

    void Start()
    {
        StartCoroutine(SpawnLoop());
    }

    IEnumerator SpawnLoop()
    {
        while (true)
        {
            yield return new WaitForSeconds(spawnInterval);

            if (_activeEnemies < maxEnemiesAtOnce && spawnPoints.Length > 0)
            {
                SpawnEnemy();
            }
        }
    }

    void SpawnEnemy()
    {
        Transform spawnPoint = spawnPoints[Random.Range(0, spawnPoints.Length)];
        GameObject enemyObj = Instantiate(enemyPrefab, spawnPoint.position, spawnPoint.rotation);
        _activeEnemies++;

        // Track when enemy is disabled so we can decrement count
        enemyObj.GetComponent<Enemy>().OnEnemyDeactivated += () => _activeEnemies--;
    }
}