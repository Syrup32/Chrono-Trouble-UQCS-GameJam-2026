using UnityEngine;

public class Enemy : MonoBehaviour
{
    [Header("Settings")]
    public float timeToShoot = 3f;
    public int pointValue = 100;
    public bool isMoving = false;
    public float moveSpeed = 2f;
    public Vector3 moveDirection = Vector3.right;

    [Header("State")]
    public bool isDead = false;

    private float _timer;
    private Vector3 _startPosition;

    void OnEnable()
    {
        isDead = false;
        _timer = timeToShoot;
        _startPosition = transform.position;
    }

    void Update()
    {
        if (isDead) return;

        _timer -= Time.deltaTime;

        if (_timer <= 0f)
        {
            ShootPlayer();
        }

        if (isMoving)
        {
            transform.position += moveDirection * moveSpeed * Time.deltaTime;

            Vector3 viewPos = Camera.main.WorldToViewportPoint(transform.position);
            if (viewPos.x < 0.05f || viewPos.x > 0.95f)
                moveDirection.x = -moveDirection.x;
            if (viewPos.y < 0.05f || viewPos.y > 0.95f)
                moveDirection.y = -moveDirection.y;
        }
    }

    void ShootPlayer()
    {
        if (isDead) return;
        isDead = true;
        GameManager.Instance?.EnemyShot();
        gameObject.SetActive(false);
    }

    public void GetHit(int playerIndex)
    {
        if (isDead) return;
        isDead = true;
        GameManager.Instance?.AddScore(pointValue, playerIndex);
        gameObject.SetActive(false);
    }

    public System.Action OnEnemyDeactivated;

    void OnDisable()
    {
        OnEnemyDeactivated?.Invoke();
    }
}