using System.Collections;
using UnityEngine;

public class Enemy : MonoBehaviour
{
    [Header("Health")]
    public int maxHitPoints = 1;
    private int _currentHitPoints;

    [Header("Movement")]
    public bool isMoving = false;
    public float moveSpeed = 2f;
    public Vector3 moveDirection = Vector3.right;

    [Header("Scoring")]
    public int maxScore = 500;
    public int minScore = 50;
    public float scoreDecayRate = 50f;

    [Header("Attack Settings")]
    public bool canAttack = true;
    public float timeBeforeAttacking = 3f;
    public float attackInterval = 1f;
    [Range(0f, 1f)]
    public float hitProbability = 0.8f;

    [Header("Audio")]
    public AudioClip attackSound;
    public AudioClip deathSound;
    public AudioClip hitSound;
    [Range(0f, 1f)]
    public float attackSoundVolume = 1f;
    [Range(0f, 1f)]
    public float deathSoundVolume = 1f;
    [Range(0f, 1f)]
    public float hitSoundVolume = 1f;

    [Header("State")]
    public bool isDead = false;

    private float _timeAlive = 0f;
    private float _attackTimer = 0f;
    private bool _isAttacking = false;
    private Vector3 _startPosition;
    private AudioSource _audioSource;

    void Awake()
    {
        _audioSource = gameObject.AddComponent<AudioSource>();
    }

    void OnEnable()
    {
        isDead = false;
        _timeAlive = 0f;
        _attackTimer = 0f;
        _isAttacking = false;
        _currentHitPoints = maxHitPoints;
        _startPosition = transform.position;
    }

    void Update()
    {
        if (isDead) return;

        _timeAlive += Time.deltaTime;

        if (canAttack)
        {
            if (!_isAttacking && _timeAlive >= timeBeforeAttacking)
            {
                _isAttacking = true;
                _attackTimer = attackInterval;
            }

            if (_isAttacking)
            {
                _attackTimer += Time.deltaTime;
                if (_attackTimer >= attackInterval)
                {
                    _attackTimer = 0f;
                    DamagePlayer();
                }
            }
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

    void DamagePlayer()
    {
        if (isDead) return;

        if (Random.value > hitProbability) return;

        bool p1Connected = GunInputReader.Instance.players[0].isConnected;
        bool p2Connected = GunInputReader.Instance.players[1].isConnected;

        if (!p1Connected && !p2Connected) return;

        int targetPlayer = -1;

        if (p1Connected && p2Connected)
        {
            targetPlayer = Random.Range(0, 2);
        }
        else if (p1Connected)
        {
            targetPlayer = 0;
        }
        else if (p2Connected)
        {
            targetPlayer = 1;
        }

        if (targetPlayer >= 0)
        {
            if (attackSound != null)
                _audioSource.PlayOneShot(attackSound, attackSoundVolume);

            GameManager.Instance?.EnemyShot(targetPlayer);
        }
    }

    int CalculateScore()
    {
        int score = maxScore - Mathf.RoundToInt(_timeAlive * scoreDecayRate);
        return Mathf.Max(score, minScore);
    }

    public void GetHit(int playerIndex)
    {
        if (isDead) return;

        _currentHitPoints--;

        if (_currentHitPoints > 0)
        {
            if (hitSound != null)
                _audioSource.PlayOneShot(hitSound, hitSoundVolume);
            return;
        }

        isDead = true;

        int score = CalculateScore();
        GameManager.Instance?.AddScore(score, playerIndex);

        if (deathSound != null)
        {
            _audioSource.PlayOneShot(deathSound, deathSoundVolume);
            StartCoroutine(DeactivateAfterSound(deathSound.length));
        }
        else
        {
            gameObject.SetActive(false);
        }
    }

    IEnumerator DeactivateAfterSound(float delay)
    {
        // Disable visuals and collision immediately
        GetComponent<Renderer>().enabled = false;
        GetComponent<Collider>().enabled = false;

        // Wait for sound to finish
        yield return new WaitForSeconds(delay);

        // Re-enable for next spawn
        GetComponent<Renderer>().enabled = true;
        GetComponent<Collider>().enabled = true;

        gameObject.SetActive(false);
    }

    public System.Action OnEnemyDeactivated;

    void OnDisable()
    {
        OnEnemyDeactivated?.Invoke();
    }
}