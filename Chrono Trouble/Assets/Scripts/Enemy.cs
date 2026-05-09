using UnityEngine;
using System.Collections;
using System.Collections.Generic;

public class Enemy : MonoBehaviour
{
    [Header("Health")]
    public int maxHitPoints = 1;
    protected int _currentHitPoints;

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

    protected float _timeAlive = 0f;
    protected float _attackTimer = 0f;
    protected bool _isAttacking = false;
    protected AudioSource _audioSource;

    public System.Action OnEnemyDeactivated;

    protected virtual void Awake()
    {
        _audioSource = gameObject.AddComponent<AudioSource>();
    }

    protected virtual void OnEnable()
    {
        isDead = false;
        _timeAlive = 0f;
        _attackTimer = 0f;
        _isAttacking = false;
        _currentHitPoints = maxHitPoints;
    }

    protected virtual void Update()
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
    }

    protected void DamagePlayer()
    {
        if (isDead) return;
        if (Random.value > hitProbability) return;

        bool p1Connected = GunInputReader.Instance.players[0].isConnected;
        bool p2Connected = GunInputReader.Instance.players[1].isConnected;

        if (!p1Connected && !p2Connected) return;

        int targetPlayer = -1;

        if (p1Connected && p2Connected)
            targetPlayer = Random.Range(0, 2);
        else if (p1Connected)
            targetPlayer = 0;
        else if (p2Connected)
            targetPlayer = 1;

        if (targetPlayer >= 0)
        {
            if (attackSound != null)
                _audioSource.PlayOneShot(attackSound, attackSoundVolume);
            GameManager.Instance?.EnemyShot(targetPlayer);
        }
    }

    protected int CalculateScore()
    {
        int score = maxScore - Mathf.RoundToInt(_timeAlive * scoreDecayRate);
        return Mathf.Max(score, minScore);
    }

    public virtual void GetHit(int playerIndex, int damage = 1)
    {
        if (isDead) return;

        _currentHitPoints -= damage;

        if (_currentHitPoints > 0)
        {
            if (hitSound != null)
                _audioSource.PlayOneShot(hitSound, hitSoundVolume);
            return;
        }

        Die(playerIndex);
    }

    protected virtual void Die(int playerIndex)
    {
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
        Renderer rend = GetComponent<Renderer>();
        Collider col = GetComponent<Collider>();

        if (rend) rend.enabled = false;
        if (col) col.enabled = false;

        yield return new WaitForSeconds(delay);

        if (rend) rend.enabled = true;
        if (col) col.enabled = true;

        gameObject.SetActive(false);
    }

    protected void OnDisable()
    {
        OnEnemyDeactivated?.Invoke();
    }
}