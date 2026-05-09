using UnityEngine;

public class ZombieEnemy : Enemy
{
    [Header("Zombie Settings")]
    public float moveSpeed = 2f;
    public float attackDistance = 3f;
    public float stopDistance = 1.5f;

    private Transform _cameraTransform;
    private bool _inAttackRange = false;
    private bool _stopped = false;
    private Vector3 _stoppingOffset;

    protected override void Awake()
    {
        base.Awake();
        maxHitPoints = 3;
        canAttack = false;
    }

    protected override void OnEnable()
    {
        base.OnEnable();
        _cameraTransform = Camera.main.transform;
        _inAttackRange = false;
        _stopped = false;
        canAttack = false;

        // Random lateral offset so zombies don't stack on same spot
        _stoppingOffset = new Vector3(
            Random.Range(-2f, 2f),
            0f,
            0f
        );
    }

    protected override void Update()
    {
        if (isDead) return;

        _timeAlive += Time.deltaTime;

        if (!_stopped)
        {
            // Target point is stopDistance in front of camera
            // with a unique lateral offset per zombie
            Vector3 targetPosition = _cameraTransform.position
                + _cameraTransform.forward * stopDistance
                + _cameraTransform.right * _stoppingOffset.x;
            targetPosition.y = transform.position.y;

            float distanceToTarget = Vector3.Distance(
                transform.position, targetPosition);

            if (distanceToTarget > 0.1f)
            {
                Vector3 direction = (targetPosition
                    - transform.position).normalized;
                transform.position += direction * moveSpeed * Time.deltaTime;

                // Face the camera while walking
                transform.LookAt(new Vector3(
                    _cameraTransform.position.x,
                    transform.position.y,
                    _cameraTransform.position.z));
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
}