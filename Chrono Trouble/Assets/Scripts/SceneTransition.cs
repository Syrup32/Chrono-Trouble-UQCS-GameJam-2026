using UnityEngine;
using UnityEngine.UI;
using UnityEngine.SceneManagement;
using System.Collections;

public class SceneTransition : MonoBehaviour
{
    public static SceneTransition Instance;

    [Header("Settings")]
    public float fadeDuration = 0.5f;

    private Image _fadePanel;
    private bool _isFading = false;

    void Awake()
    {
        if (Instance != null)
        {
            Destroy(gameObject);
            return;
        }
        Instance = this;
        DontDestroyOnLoad(gameObject);

        // Create the fade panel
        SetupFadePanel();

        // Start fully black then fade out
        StartCoroutine(FadeOut());
    }

    void SetupFadePanel()
    {
        // Create a canvas that sits on top of everything
        GameObject canvasObj = new GameObject("FadeCanvas");
        canvasObj.transform.SetParent(transform);

        Canvas canvas = canvasObj.AddComponent<Canvas>();
        canvas.renderMode = RenderMode.ScreenSpaceOverlay;
        canvas.sortingOrder = 999; // Always on top

        canvasObj.AddComponent<CanvasScaler>();
        canvasObj.AddComponent<GraphicRaycaster>();

        // Create the black panel
        GameObject panelObj = new GameObject("FadePanel");
        panelObj.transform.SetParent(canvasObj.transform, false);

        _fadePanel = panelObj.AddComponent<Image>();
        _fadePanel.color = Color.black;

        // Stretch to fill screen
        RectTransform rect = _fadePanel.GetComponent<RectTransform>();
        rect.anchorMin = Vector2.zero;
        rect.anchorMax = Vector2.one;
        rect.offsetMin = Vector2.zero;
        rect.offsetMax = Vector2.zero;
    }

    public void LoadScene(int sceneIndex)
    {
        if (_isFading) return;
        StartCoroutine(TransitionToScene(sceneIndex));
    }

    IEnumerator TransitionToScene(int sceneIndex)
    {
        _isFading = true;
        yield return StartCoroutine(FadeIn());
        SceneManager.LoadScene(sceneIndex);
        yield return StartCoroutine(FadeOut());
        _isFading = false;
    }

    IEnumerator FadeIn()
    {
        float elapsed = 0f;
        Color color = _fadePanel.color;
        color.a = 0f;
        _fadePanel.color = color;

        while (elapsed < fadeDuration)
        {
            elapsed += Time.deltaTime;
            color.a = Mathf.Clamp01(elapsed / fadeDuration);
            _fadePanel.color = color;
            yield return null;
        }

        color.a = 1f;
        _fadePanel.color = color;
    }

    IEnumerator FadeOut()
    {
        float elapsed = 0f;
        Color color = _fadePanel.color;
        color.a = 1f;
        _fadePanel.color = color;

        while (elapsed < fadeDuration)
        {
            elapsed += Time.deltaTime;
            color.a = 1f - Mathf.Clamp01(elapsed / fadeDuration);
            _fadePanel.color = color;
            yield return null;
        }

        color.a = 0f;
        _fadePanel.color = color;
    }
}