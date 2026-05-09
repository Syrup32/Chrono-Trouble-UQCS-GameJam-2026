using UnityEngine;
using System.Collections;
using System.Collections.Generic;

public class BirdSwarm : Enemy
{
    [Header("Swarm Settings")]
    public int birdCount = 5;
    public float swarmRadius = 1.5f;
    public float flySpeed = 3f;
    public float attackDistance = 4f;
    public float stopDistance = 1f;
    public GameObject birdVisualPrefab;

    private List<Transform> _birds = new List<Transform>();
    private Transform _cameraTransform;
    private bool _inAttackRange = false;
    private bool _stopped = false;
    private Vector3 _stoppingOffset;

    protected override void Awake()
    {
        base.Awake();
        maxHitPoints = birdCount;
        canAttack = false;

        // Disable parent collider — birds have their own colliders
        Collider col = GetComponent<Collider>();
        if (col != null) col.enabled = false;
    }

    protected override void OnEnable()
    {
        base.OnEnable();
        _cameraTransform = Camera.main.transform;
        _inAttackRange = false;
        _stopped = false;
        canAttack = false;
        _currentHitPoints = birdCount;

        _stoppingOffset = new Vector3(
            Random.Range(-2f, 2f),
            0f,
            0f
        );

        SpawnBirdVisuals();
    }

    void SpawnBirdVisuals()
    {
        foreach (Transform b in _birds)
            if (b != null) Destroy(b.gameObject);
        _birds.Clear();

        if (birdVisualPrefab == null) return;

        for (int i = 0; i < birdCount; i++)
        {
            float angle = (360f / birdCount) * i * Mathf.Deg2Rad;
            Vector3 offset = new Vector3(
                Mathf.Cos(angle) * swarmRadius,
                0f,
                Mathf.Sin(angle) * swarmRadius
            );

            GameObject bird = Instantiate(birdVisualPrefab,
                transform.position + offset, Quaternion.identity, transform);

            // Add collider to each bird so it can be shot
            if (bird.GetComponent<Collider>() == null)
                bird.AddComponent<SphereCollider>();

            // Add BirdHitProxy so shooting a bird hits the swarm
            BirdHitProxy proxy = bird.AddComponent<BirdHitProxy>();
            proxy.parentSwarm = this;

            // Set to Enemy layer so ShootingSystem raycast detects it
            bird.layer = LayerMask.NameToLayer("Enemy");

            _birds.Add(bird.transform);
        }
    }

    protected override void Update()
    {
        if (isDead) return;

        _timeAlive += Time.deltaTime;

        if (!_stopped)
        {
            Vector3 targetPosition = _cameraTransform.position
                + _cameraTransform.forward * stopDistance
                + _cameraTransform.right * _stoppingOffset.x;
            targetPosition.y = _cameraTransform.position.y;

            float distanceToTarget = Vector3.Distance(
                transform.position, targetPosition);

            if (distanceToTarget > 0.1f)
            {
                Vector3 direction = (targetPosition - transform.position).normalized;
                transform.position += direction * flySpeed * Time.deltaTime;
            }
            else
            {
                _stopped = true;
                transform.position = targetPosition;
            }

            float distanceToCamera = Vector3.Distance(
                transform.position, _cameraTransform.position);

            if (!_inAttackRange && distanceToCamera <= attackDistance)
            {
                _inAttackRange = true;
                canAttack = true;
                _isAttacking = true;
                _attackTimer = attackInterval;
            }
        }

        // Animate birds with individual bobbing
        for (int i = 0; i < _birds.Count; i++)
        {
            if (_birds[i] == null) continue;

            float angle = (360f / birdCount) * i * Mathf.Deg2Rad;
            Vector3 baseOffset = new Vector3(
                Mathf.Cos(angle) * swarmRadius,
                0f,
                Mathf.Sin(angle) * swarmRadius
            );

            float bob = Mathf.Sin(Time.time * 2f + i * 1.2f) * 0.2f;
            _birds[i].localPosition = Vector3.Lerp(
                _birds[i].localPosition,
                baseOffset + Vector3.up * bob,
                Time.deltaTime * 3f
            );
        }

        if (canAttack && _isAttacking)
        {
            _attackTimer += Time.deltaTime;
            if (_attackTimer >= attackInterval)
            {
                _attackTimer = 0f;
                DamagePlayer();
            }
        }
    }

    public override void GetHit(int playerIndex, int damage = 1)
    {
        if (isDead) return;

        if (_birds.Count > 0)
        {
            Transform lastBird = _birds[_birds.Count - 1];
            _birds.RemoveAt(_birds.Count - 1);
            if (lastBird != null) Destroy(lastBird.gameObject);
        }

        base.GetHit(playerIndex, damage);
    }
}