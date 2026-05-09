using UnityEngine;
using System.Collections;

public class InfantryEnemy : Enemy
{
    [Header("Cover Settings")]
    public float riseTime = 0.5f;
    public Vector3 hiddenOffset = new Vector3(0, -2f, 0);

    private Vector3 _exposedPosition;
    private bool _isRising = false;

    protected override void Awake()
    {
        base.Awake();
        maxHitPoints = 2;
    }

    protected override void OnEnable()
    {
        base.OnEnable();
        _exposedPosition = transform.position;
        transform.position = _exposedPosition + hiddenOffset;
        StartCoroutine(RiseFromCover());
    }

    IEnumerator RiseFromCover()
    {
        _isRising = true;
        float elapsed = 0f;
        Vector3 startPos = transform.position;

        while (elapsed < riseTime)
        {
            elapsed += Time.deltaTime;
            transform.position = Vector3.Lerp(
                startPos, _exposedPosition, elapsed / riseTime);
            yield return null;
        }

        transform.position = _exposedPosition;
        _isRising = false;
    }

    protected override void Update()
    {
        if (_isRising) return;
        base.Update();
    }
}