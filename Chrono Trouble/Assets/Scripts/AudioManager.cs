using UnityEngine;

public class AudioManager : MonoBehaviour
{
    public static AudioManager Instance;

    [Header("Audio Clips")]
    public AudioClip gunshotSound;
    public AudioClip reloadSound;
    public AudioClip emptyClickSound; // optional - when firing with no ammo
    public AudioClip headshotSound;  
    

    [Header("Settings")]
    public float gunshotVolume = 1f;
    public float reloadVolume = 1f;
    public float headshotVolume = 1f;

    private AudioSource _audioSource;

    void Awake()
    {
        if (Instance != null)
        {
            Destroy(gameObject);
            return;
        }
        Instance = this;
        DontDestroyOnLoad(gameObject);

        _audioSource = gameObject.AddComponent<AudioSource>();
    }

    public void PlayGunshot()
    {
        if (gunshotSound == null) return;
        _audioSource.PlayOneShot(gunshotSound, gunshotVolume);
    }

    public void PlayReload()
    {
        if (reloadSound == null) return;
        _audioSource.PlayOneShot(reloadSound, reloadVolume);
    }

    public void PlayEmptyClick()
    {
        if (emptyClickSound == null) return;
        _audioSource.PlayOneShot(emptyClickSound, gunshotVolume);
    }
    
    public void PlayHeadshot()
    {
        if (headshotSound == null) return;
        _audioSource.PlayOneShot(headshotSound, headshotVolume);
    }
}