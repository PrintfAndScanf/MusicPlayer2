#include "stdafx.h"
#include "BassCore.h"
#include "Common.h"
#include "AudioCommon.h"
#include "MusicPlayer2.h"

CBASSMidiLibrary CBassCore::m_bass_midi_lib;
CBassCore::MidiLyricInfo CBassCore::m_midi_lyric;
BASS_MIDI_FONT CBassCore::m_sfont{};

CBassCore::CBassCore()
{
}


CBassCore::~CBassCore()
{
}

void CBassCore::InitCore()
{
    //获取当前的音频输出设备
    BASS_DEVICEINFO device_info;
    int rtn;
    int device_index{ 1 };
    theApp.m_output_devices.clear();
    DeviceInfo device{};
    device.index = -1;
    device.name = CCommon::LoadText(IDS_DEFAULT_OUTPUT_DEVICE);
    theApp.m_output_devices.push_back(device);
    while (true)
    {
        device = DeviceInfo{};
        rtn = BASS_GetDeviceInfo(device_index, &device_info);
        if (rtn == 0)
            break;
        device.index = device_index;
        if (device_info.name != nullptr)
            device.name = CCommon::StrToUnicode(string(device_info.name));
        if (device_info.driver != nullptr)
            device.driver = CCommon::StrToUnicode(string(device_info.driver));
        device.flags = device_info.flags;
        theApp.m_output_devices.push_back(device);
        device_index++;
    }

    for (size_t i{}; i < theApp.m_output_devices.size(); i++)
    {
        if (theApp.m_output_devices[i].name == theApp.m_play_setting_data.output_device)
        {
            theApp.m_play_setting_data.device_selected = i;
            break;
        }
    }

    //初始化BASE音频库
    BASS_Init(
        theApp.m_output_devices[theApp.m_play_setting_data.device_selected].index,		//播放设备
        44100,//输出采样率44100（常用值）
        BASS_DEVICE_CPSPEAKERS,//信号，BASS_DEVICE_CPSPEAKERS 注释原文如下：
        /* Use the Windows control panel setting to detect the number of speakers.*/
        /* Soundcards generally have their own control panel to set the speaker config,*/
        /* so the Windows control panel setting may not be accurate unless it matches that.*/
        /* This flag has no effect on Vista, as the speakers are already accurately detected.*/
        theApp.m_pMainWnd->m_hWnd,//程序窗口,0用于控制台程序
        NULL//类标识符,0使用默认值
    );

    //向支持的文件列表插入原生支持的文件格式
    CAudioCommon::m_surpported_format.clear();
    SupportedFormat format;
    format.description = CCommon::LoadText(IDS_BASIC_AUDIO_FORMAT);
    format.extensions.push_back(L"mp3");
    format.extensions.push_back(L"wma");
    format.extensions.push_back(L"wav");
    format.extensions.push_back(L"flac");
    format.extensions.push_back(L"ogg");
    format.extensions.push_back(L"oga");
    format.extensions.push_back(L"m4a");
    format.extensions.push_back(L"mp4");
    format.extensions.push_back(L"cue");
    format.extensions.push_back(L"mp2");
    format.extensions.push_back(L"mp1");
    format.extensions.push_back(L"aif");
    format.extensions_list = L"*.mp3;*.wma;*.wav;*.flac;*.ogg;*.oga;*.m4a;*.mp4;*.cue;*.mp2;*.mp1;*.aif";
    CAudioCommon::m_surpported_format.push_back(format);
    CAudioCommon::m_all_surpported_extensions = format.extensions;
    //载入BASS插件
    wstring plugin_dir;
    plugin_dir = theApp.m_local_dir + L"Plugins\\";
    vector<wstring> plugin_files;
    CCommon::GetFiles(plugin_dir + L"*.dll", plugin_files);		//获取Plugins目录下所有的dll文件的文件名
    m_plugin_handles.clear();
    for (const auto& plugin_file : plugin_files)
    {
        //加载插件
        HPLUGIN handle = BASS_PluginLoad((plugin_dir + plugin_file).c_str(), 0);
        m_plugin_handles.push_back(handle);
        //获取插件支持的音频文件类型
        const BASS_PLUGININFO* plugin_info = BASS_PluginGetInfo(handle);
        if (plugin_info == nullptr)
            continue;
        format.file_name = plugin_file;
        format.description = CCommon::ASCIIToUnicode(plugin_info->formats->name);	//插件支持文件类型的描述
        format.extensions_list = CCommon::ASCIIToUnicode(plugin_info->formats->exts);	//插件支持文件扩展名列表
        //解析扩展名列表到vector
        format.extensions.clear();
        size_t index = 0, last_index = 0;
        while (true)
        {
            index = format.extensions_list.find(L"*.", index + 1);
            wstring ext{ format.extensions_list.substr(last_index + 2, index - last_index - 2) };
            if (!ext.empty() && ext.back() == L';')
                ext.pop_back();
            format.extensions.push_back(ext);
            if (!CCommon::IsItemInVector(CAudioCommon::m_all_surpported_extensions, ext))
                CAudioCommon::m_all_surpported_extensions.push_back(ext);
            if (index == wstring::npos)
                break;
            last_index = index;
        }
        CAudioCommon::m_surpported_format.push_back(format);

        //载入MIDI音色库，用于播放MIDI
        if (format.description == L"MIDI")
        {
            m_bass_midi_lib.Init(plugin_dir + plugin_file);
            m_sfont_name = CCommon::LoadText(_T("<"), IDS_NONE, _T(">"));
            m_sfont.font = 0;
            if (m_bass_midi_lib.IsSuccessed())
            {
                wstring sf2_path = theApp.m_general_setting_data.sf2_path;
                if (!CCommon::FileExist(sf2_path))		//如果设置的音色库路径不存在，则从.\Plugins\soundfont\目录下查找音色库文件
                {
                    vector<wstring> sf2s;
                    CCommon::GetFiles(plugin_dir + L"soundfont\\*.sf2", sf2s);
                    if (!sf2s.empty())
                        sf2_path = plugin_dir + L"soundfont\\" + sf2s[0];
                }
                if (CCommon::FileExist(sf2_path))
                {
                    m_sfont.font = m_bass_midi_lib.BASS_MIDI_FontInit(sf2_path.c_str(), BASS_UNICODE);
                    if (m_sfont.font == 0)
                    {
                        CString info;
                        info = CCommon::LoadTextFormat(IDS_SOUND_FONT_LOAD_FAILED, { sf2_path });
                        theApp.WriteErrorLog(info.GetString());
                        m_sfont_name = CCommon::LoadText(_T("<"), IDS_LOAD_FAILED, _T(">"));
                    }
                    else
                    {
                        //获取音色库信息
                        BASS_MIDI_FONTINFO sfount_info;
                        m_bass_midi_lib.BASS_MIDI_FontGetInfo(m_sfont.font, &sfount_info);
                        m_sfont_name = CCommon::StrToUnicode(sfount_info.name);
                    }
                    m_sfont.preset = -1;
                    m_sfont.bank = 0;
                }
            }
        }
    }

}

void CBassCore::UnInitCore()
{
    BASS_Stop();	//停止输出
    BASS_Free();	//释放Bass资源
    if (m_bass_midi_lib.IsSuccessed() && m_sfont.font != 0)
        m_bass_midi_lib.BASS_MIDI_FontFree(m_sfont.font);
    m_bass_midi_lib.UnInit();
    for (const auto& handle : m_plugin_handles)		//释放插件句柄
    {
        BASS_PluginFree(handle);
    }
}

unsigned int CBassCore::GetHandle()
{
    return m_musicStream;
}

std::wstring CBassCore::GetAudioType()
{
    return CAudioCommon::GetBASSChannelDescription(m_channel_info.ctype);
}

void CBassCore::MidiLyricSync(HSYNC handle, DWORD channel, DWORD data, void * user)
{
    if (!m_bass_midi_lib.IsSuccessed())
        return;
    m_midi_lyric.midi_no_lyric = false;
    BASS_MIDI_MARK mark;
    m_bass_midi_lib.BASS_MIDI_StreamGetMark(channel, (DWORD)user, data, &mark); // get the lyric/text
    if (mark.text[0] == '@') return; // skip info
    if (mark.text[0] == '\\')
    {
        // clear display
        m_midi_lyric.midi_lyric.clear();
    }
    else if (mark.text[0] == '/')
    {
        m_midi_lyric.midi_lyric += L"\r\n";
        const char* text = mark.text + 1;
        m_midi_lyric.midi_lyric += CCommon::StrToUnicode(text, CodeType::ANSI);
    }
    else
    {
        m_midi_lyric.midi_lyric += CCommon::StrToUnicode(mark.text, CodeType::ANSI);
    }
}

void CBassCore::MidiEndSync(HSYNC handle, DWORD channel, DWORD data, void * user)
{
    m_midi_lyric.midi_lyric.clear();
}

void CBassCore::GetMidiPosition()
{
    if (m_is_midi)
    {
        //获取midi音乐的进度并转换成节拍数。（其中+ (m_midi_info.ppqn / 4)的目的是修正显示的节拍不准确的问题）
        m_midi_info.midi_position = static_cast<int>((BASS_ChannelGetPosition(m_musicStream, BASS_POS_MIDI_TICK) + (m_midi_info.ppqn / 4)) / m_midi_info.ppqn);
    }

}

void CBassCore::SetFXHandle()
{
    if (m_musicStream == 0) return;
    //if (!m_equ_enable) return;
    //设置每个均衡器通道的句柄
    for (int i{}; i < EQU_CH_NUM; i++)
    {
        m_equ_handle[i] = BASS_ChannelSetFX(m_musicStream, BASS_FX_DX8_PARAMEQ, 1);
    }
    //设置混响的句柄
    m_reverb_handle = BASS_ChannelSetFX(m_musicStream, BASS_FX_DX8_REVERB, 1);
}

void CBassCore::RemoveFXHandle()
{
    if (m_musicStream == 0) return;
    //移除每个均衡器通道的句柄
    for (int i{}; i < EQU_CH_NUM; i++)
    {
        if (m_equ_handle[i] != 0)
        {
            BASS_ChannelRemoveFX(m_musicStream, m_equ_handle[i]);
            m_equ_handle[i] = 0;
        }
    }
    //移除混响的句柄
    if (m_reverb_handle != 0)
    {
        BASS_ChannelRemoveFX(m_musicStream, m_reverb_handle);
        m_reverb_handle = 0;
    }
}

void CBassCore::GetBASSAudioInfo(HSTREAM hStream, const wchar_t* file_path, SongInfo & song_info, bool get_tag)
{
    //获取长度
    song_info.lengh = CBassCore::GetBASSSongLength(hStream);
    //获取比特率
    float bitrate{};
    BASS_ChannelGetAttribute(hStream, BASS_ATTRIB_BITRATE, &bitrate);
    song_info.bitrate = static_cast<int>(bitrate + 0.5f);
    if(get_tag)
    {
        CAudioTag audio_tag(hStream, file_path, song_info);
        audio_tag.GetAudioTag(theApp.m_general_setting_data.id3v2_first);
        //获取midi音乐的标题
        if (CBassCore::m_bass_midi_lib.IsSuccessed() && audio_tag.GetAudioType() == AU_MIDI)
        {
            BASS_MIDI_MARK mark;
            if (CBassCore::m_bass_midi_lib.BASS_MIDI_StreamGetMark(hStream, BASS_MIDI_MARK_TRACK, 0, &mark) && !mark.track)
            {
                song_info.title = CCommon::StrToUnicode(mark.text);
                song_info.info_acquired = true;
            }
        }
    }
}

int CBassCore::GetChannels()
{
    return m_channel_info.chans;
}

int CBassCore::GetFReq()
{
    return m_channel_info.freq;
}

const wstring& CBassCore::GetSoundFontName()
{
    return m_sfont_name;
}

void CBassCore::Open(const wchar_t * file_path)
{
    m_file_path = file_path;
    if (CCommon::IsURL(m_file_path))
        m_musicStream = BASS_StreamCreateURL(file_path, 0, BASS_SAMPLE_FLOAT, NULL, NULL);
    else
        m_musicStream = BASS_StreamCreateFile(FALSE, /*(GetCurrentFilePath()).c_str()*/file_path, 0, 0, BASS_SAMPLE_FLOAT);
    BASS_ChannelGetInfo(m_musicStream, &m_channel_info);
    m_is_midi = (CAudioCommon::GetAudioTypeByBassChannel(m_channel_info.ctype) == AudioType::AU_MIDI);
    if (m_bass_midi_lib.IsSuccessed() && m_is_midi && m_sfont.font != 0)
        m_bass_midi_lib.BASS_MIDI_StreamSetFonts(m_musicStream, &m_sfont, 1);

    //如果文件是MIDI音乐，则打开时获取MIDI音乐的信息
    if (m_is_midi && m_bass_midi_lib.IsSuccessed())
    {
        //获取MIDI音乐信息
        BASS_ChannelGetAttribute(m_musicStream, BASS_ATTRIB_MIDI_PPQN, &m_midi_info.ppqn); // get PPQN value
        m_midi_info.midi_length = static_cast<int>(BASS_ChannelGetLength(m_musicStream, BASS_POS_MIDI_TICK) / m_midi_info.ppqn);
        m_midi_info.tempo = m_bass_midi_lib.BASS_MIDI_StreamGetEvent(m_musicStream, 0, MIDI_EVENT_TEMPO);
        m_midi_info.speed = 60000000 / m_midi_info.tempo;
        //获取MIDI音乐内嵌歌词
        BASS_MIDI_MARK mark;
        m_midi_lyric.midi_lyric.clear();
        if (m_bass_midi_lib.BASS_MIDI_StreamGetMark(m_musicStream, BASS_MIDI_MARK_LYRIC, 0, &mark)) // got lyrics
            BASS_ChannelSetSync(m_musicStream, BASS_SYNC_MIDI_MARK, BASS_MIDI_MARK_LYRIC, MidiLyricSync, (void*)BASS_MIDI_MARK_LYRIC);
        else if (m_bass_midi_lib.BASS_MIDI_StreamGetMark(m_musicStream, BASS_MIDI_MARK_TEXT, 20, &mark)) // got text instead (over 20 of them)
            BASS_ChannelSetSync(m_musicStream, BASS_SYNC_MIDI_MARK, BASS_MIDI_MARK_TEXT, MidiLyricSync, (void*)BASS_MIDI_MARK_TEXT);
        BASS_ChannelSetSync(m_musicStream, BASS_SYNC_END, 0, MidiEndSync, 0);
        m_midi_lyric.midi_no_lyric = true;
    }
    SetFXHandle();
}

void CBassCore::Close()
{
    if(KillTimer(theApp.m_pMainWnd->GetSafeHwnd(), FADE_TIMER_ID))
        BASS_ChannelStop(m_musicStream);
    RemoveFXHandle();
    BASS_StreamFree(m_musicStream);
}

void CBassCore::Play()
{
    if (theApp.m_play_setting_data.fade_effect)     //如果设置了播放时音量淡入淡出
    {
        KillTimer(theApp.m_pMainWnd->GetSafeHwnd(), FADE_TIMER_ID);
        int pos = GetCurPosition();
        pos -= theApp.m_play_setting_data.fade_time;
        if (pos < 0)
            pos = 0;
        SetCurPosition(pos);
        BASS_ChannelSetAttribute(m_musicStream, BASS_ATTRIB_VOL, 0);        //先将音量设为0
        BASS_ChannelPlay(m_musicStream, FALSE);
        float volume = static_cast<float>(m_volume) / 100;
        BASS_ChannelSlideAttribute(m_musicStream, BASS_ATTRIB_VOL, volume, theApp.m_play_setting_data.fade_time);   //音量渐变到原来的音量
    }
    else
    {
        BASS_ChannelPlay(m_musicStream, FALSE);
    }
}

void CBassCore::Pause()
{
    if (theApp.m_play_setting_data.fade_effect)     //如果设置了播放时音量淡入淡出
    {
        BASS_ChannelSlideAttribute(m_musicStream, BASS_ATTRIB_VOL, 0, theApp.m_play_setting_data.fade_time);        //音量渐变到0
        KillTimer(theApp.m_pMainWnd->GetSafeHwnd(), FADE_TIMER_ID);
        //设置一个淡出时间的定时器
        SetTimer(theApp.m_pMainWnd->GetSafeHwnd(), FADE_TIMER_ID, theApp.m_play_setting_data.fade_time, [](HWND Arg1, UINT Arg2, UINT_PTR Arg3, DWORD Arg4)
        {
            KillTimer(theApp.m_pMainWnd->GetSafeHwnd(), FADE_TIMER_ID);
            BASS_ChannelPause(CPlayer::GetInstance().GetPlayerCore()->GetHandle());     //当定时器器触发时，即音量已经渐变到0，执行暂停操作
        });
    }
    else
    {
        BASS_ChannelPause(m_musicStream);
    }
}

void CBassCore::Stop()
{
    if (theApp.m_play_setting_data.fade_effect)
    {
        BASS_ChannelSlideAttribute(m_musicStream, BASS_ATTRIB_VOL, 0, theApp.m_play_setting_data.fade_time);
        KillTimer(theApp.m_pMainWnd->GetSafeHwnd(), FADE_TIMER_ID);
        SetTimer(theApp.m_pMainWnd->GetSafeHwnd(), FADE_TIMER_ID, theApp.m_play_setting_data.fade_time, [](HWND Arg1, UINT Arg2, UINT_PTR Arg3, DWORD Arg4)
        {
            KillTimer(theApp.m_pMainWnd->GetSafeHwnd(), FADE_TIMER_ID);
            BASS_ChannelStop(CPlayer::GetInstance().GetPlayerCore()->GetHandle());
            BASS_ChannelSetPosition(CPlayer::GetInstance().GetPlayerCore()->GetHandle(), 0, BASS_POS_BYTE);
        });
    }
    else
    {
        BASS_ChannelStop(m_musicStream);
        SetCurPosition(0);
    }
}

void CBassCore::SetVolume(int vol)
{
    m_volume = vol;
    float volume = static_cast<float>(vol) / 100.0f;
    volume = volume * theApp.m_nc_setting_data.volume_map / 100;
    BASS_ChannelSetAttribute(m_musicStream, BASS_ATTRIB_VOL, volume);
}

int CBassCore::GetCurPosition()
{
    QWORD pos_bytes;
    pos_bytes = BASS_ChannelGetPosition(m_musicStream, BASS_POS_BYTE);
    double pos_sec;
    pos_sec = BASS_ChannelBytes2Seconds(m_musicStream, pos_bytes);
    int current_position = static_cast<int>(pos_sec * 1000);
    if (current_position == -1000) current_position = 0;

    GetMidiPosition();
    return current_position;
}

int CBassCore::GetSongLength()
{
    QWORD lenght_bytes;
    lenght_bytes = BASS_ChannelGetLength(m_musicStream, BASS_POS_BYTE);
    double length_sec;
    length_sec = BASS_ChannelBytes2Seconds(m_musicStream, lenght_bytes);
    int song_length = static_cast<int>(length_sec * 1000);
    if (song_length == -1000) song_length = 0;
    return song_length;
}

void CBassCore::SetCurPosition(int position)
{
    double pos_sec = static_cast<double>(position) / 1000.0;
    QWORD pos_bytes;
    pos_bytes = BASS_ChannelSeconds2Bytes(m_musicStream, pos_sec);
    BASS_ChannelSetPosition(m_musicStream, pos_bytes, BASS_POS_BYTE);
    m_midi_lyric.midi_lyric.clear();
    GetMidiPosition();
}

void CBassCore::GetAudioInfo(SongInfo & song_info, bool get_tag)
{
    GetBASSAudioInfo(m_musicStream, m_file_path.c_str(), song_info, get_tag);
}

void CBassCore::GetAudioInfo(const wchar_t * file_path, SongInfo & song_info, bool get_tag)
{
    HSTREAM hStream;
    hStream = BASS_StreamCreateFile(FALSE, file_path, 0, 0, BASS_SAMPLE_FLOAT);
    GetBASSAudioInfo(hStream, file_path, song_info, get_tag);
    BASS_StreamFree(hStream);
}

bool CBassCore::IsMidi()
{
    return m_is_midi;
}

bool CBassCore::IsMidiConnotPlay()
{
    return (m_is_midi && m_sfont.font == 0);
}

std::wstring CBassCore::GetMidiInnerLyric()
{
    return m_midi_lyric.midi_lyric;
}

MidiInfo CBassCore::GetMidiInfo()
{
    return m_midi_info;
}

bool CBassCore::MidiNoLyric()
{
    return m_midi_lyric.midi_no_lyric;
}

void CBassCore::ApplyEqualizer(int channel, int gain)
{
    if (channel < 0 || channel >= EQU_CH_NUM) return;
    //if (!m_equ_enable) return;
    if (gain < -15) gain = -15;
    if (gain > 15) gain = 15;
    BASS_DX8_PARAMEQ parameq;
    parameq.fBandwidth = 30;
    parameq.fCenter = FREQ_TABLE[channel];
    parameq.fGain = static_cast<float>(gain);
    BASS_FXSetParameters(m_equ_handle[channel], &parameq);

}

void CBassCore::SetReverb(int mix, int time)
{
    BASS_DX8_REVERB parareverb;
    parareverb.fInGain = 0;
    //parareverb.fReverbMix = static_cast<float>(mix) / 100 * 96 - 96;
    parareverb.fReverbMix = static_cast<float>(std::pow(static_cast<double>(mix) / 100, 0.1) * 96 - 96);
    parareverb.fReverbTime = static_cast<float>(time * 10);
    parareverb.fHighFreqRTRatio = 0.001f;
    BASS_FXSetParameters(m_reverb_handle, &parareverb);
}

void CBassCore::ClearReverb()
{
    BASS_DX8_REVERB parareverb;
    parareverb.fInGain = 0;
    parareverb.fReverbMix = -96;
    parareverb.fReverbTime = 0.001f;
    parareverb.fHighFreqRTRatio = 0.001f;
    BASS_FXSetParameters(m_reverb_handle, &parareverb);
}

void CBassCore::GetFFTData(float fft_data[128])
{
    BASS_ChannelGetData(m_musicStream, fft_data, BASS_DATA_FFT256);
}

int CBassCore::GetBASSCurrentPosition(HSTREAM hStream)
{
    QWORD pos_bytes;
    pos_bytes = BASS_ChannelGetPosition(hStream, BASS_POS_BYTE);
    double pos_sec;
    pos_sec = BASS_ChannelBytes2Seconds(hStream, pos_bytes);
    return static_cast<int>(pos_sec * 1000);
}

Time CBassCore::GetBASSSongLength(HSTREAM hStream)
{
    QWORD lenght_bytes;
    lenght_bytes = BASS_ChannelGetLength(hStream, BASS_POS_BYTE);
    double length_sec;
    length_sec = BASS_ChannelBytes2Seconds(hStream, lenght_bytes);
    int song_length_int = static_cast<int>(length_sec * 1000);
    if (song_length_int == -1000) song_length_int = 0;
    return Time(song_length_int);		//将长度转换成Time结构
}

void CBassCore::SetCurrentPosition(HSTREAM hStream, int position)
{
    double pos_sec = static_cast<double>(position) / 1000.0;
    QWORD pos_bytes;
    pos_bytes = BASS_ChannelSeconds2Bytes(hStream, pos_sec);
    BASS_ChannelSetPosition(hStream, pos_bytes, BASS_POS_BYTE);
}
