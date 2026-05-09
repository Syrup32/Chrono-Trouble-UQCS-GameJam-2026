using UnityEngine;
using System.Collections;
using System.Collections.Generic;

public class EnemySpawner : MonoBehaviour
{
    [Header("Spawning")]
    public GameObject enemyPrefab;
    public Transform[] spawnPoints;
    public float spawnInterval = 2f;
    public int maxEnemiesAtOnce = 3;

    [Header("Pooling")]
    public int poolSize = 10;

    private int _activeEnemies = 0;
    private HashSet<int> _occupiedSpawnPoints = new HashSet<int>();
    private Queue<GameObject> _pool = new Queue<GameObject>();

    void Start()
    {
        // Ensure pool is always at least as large as maxEnemiesAtOnce
        if (poolSize < maxEnemiesAtOnce)
        {
            Debug.LogWarning($"EnemySpawner: poolSize ({poolSize}) is smaller than " +
                             $"maxEnemiesAtOnce ({maxEnemiesAtOnce}). " +
                             $"Adjusting poolSize to match.");
            poolSize = maxEnemiesAtOnce;
        }

        // Pre-create all enemy objects and add to pool
        for (int i = 0; i < poolSize; i++)
        {
            GameObject obj = Instantiate(enemyPrefab);
            obj.SetActive(false);
            _pool.Enqueue(obj);
        }

        StartCoroutine(SpawnLoop());
    }

    IEnumerator SpawnLoop()
    {
        while (true)
        {
            yield return new WaitForSeconds(spawnInterval);

            if (_activeEnemies < maxEnemiesAtOnce && _pool.Count > 0)
            {
                int freeIndex = GetFreeSpawnPoint();
                if (freeIndex >= 0)
                {
                    SpawnEnemy(freeIndex);
                }
            }
        }
    }

    int GetFreeSpawnPoint()
    {
        List<int> freePoints = new List<int>();
        for (int i = 0; i < spawnPoints.Length; i++)
        {
            if (!_occupiedSpawnPoints.Contains(i))
                freePoints.Add(i);
        }

        if (freePoints.Count == 0) return -1;
        return freePoints[Random.Range(0, freePoints.Count)];
    }

    void SpawnEnemy(int spawnIndex)
    {
        Transform spawnPoint = spawnPoints[spawnIndex];

        // Get from pool
        GameObject enemyObj = _pool.Dequeue();
        enemyObj.transform.position = spawnPoint.position;
        enemyObj.transform.rotation = spawnPoint.rotation;
        enemyObj.SetActive(true);

        _activeEnemies++;
        _occupiedSpawnPoints.Add(spawnIndex);

        Enemy enemy = enemyObj.GetComponent<Enemy>();
        if (enemy != null)
        {
            int capturedIndex = spawnIndex;

            // Clear previous listeners to avoid stacking
            enemy.OnEnemyDeactivated = null;

            enemy.OnEnemyDeactivated += () =>
            {
                _activeEnemies--;
                _occupiedSpawnPoints.Remove(capturedIndex);

                // Return to pool
                _pool.Enqueue(enemyObj);
            };
        }
    }
}