#pragma once
#include <cstdint>

enum Language : uint8_t { LANG_EN = 0, LANG_JA, LANG_KO, LANG_COUNT };

struct LangStrings {
    const char* langName;

    // Status bar
    const char* driverConnected;
    const char* driverNotConnected;
    const char* driverHint;

    // Header buttons
    const char* handsOn;
    const char* handsOff;
    const char* reloadSave;
    const char* save;
    const char* addMapping;

    // Tabs
    const char* tabMappings;
    const char* tabSettings;

    // Empty state
    const char* noMappingsYet;
    const char* pressAddToBegin;
    const char* waitingForDevices;

    // Mapping card labels
    const char* controller;
    const char* tracker;
    const char* leftHand;
    const char* rightHand;
    const char* handTracking;
    const char* remove;
    const char* redirectMode;
    const char* smoothCal;
    const char* stopAdjust;
    const char* quickCal;
    const char* manualOffset;
    const char* manualOffsetOpen;

    // ON/OFF
    const char* btnOn;
    const char* btnOff;

    // Offset editor
    const char* positionM;
    const char* rotationDeg;
    const char* resetPos;
    const char* resetRot;
    const char* resetAll;
    const char* worldOffset;
    const char* worldOffsetDesc;

    // Pending card
    const char* waitingForDevicesStatus;
    const char* retry;

    // Add mapping popup
    const char* newDeviceMapping;
    const char* showAllDevices;
    const char* controllerSource;
    const char* specificDevice;
    const char* leftHandAuto;
    const char* rightHandAuto;
    const char* selectController;
    const char* selectTracker;
    const char* targetDevice;
    const char* sourceDevice;
    const char* cancel;
    const char* addMappingBtn;
    const char* noCtrlOrTracker;
    const char* noTracker;
    const char* handRoleNotFound;
    const char* mapsToLeft;
    const char* mapsToRight;

    // Settings sections
    const char* sectionLanguage;
    const char* sectionAbout;
    const char* aboutVersion;
    const char* aboutSourceLabel;
    const char* aboutTooltip;
    const char* sectionDeviceVisibility;
    const char* hideHandTrackers;
    const char* hideHandTrackersDesc;
    const char* sectionBindings;
    const char* bindingsDesc;
    const char* editBindings;
    const char* editBindingsDesc;
    const char* sectionHandTracking;
    const char* handTrackingDesc;
    const char* lockHandTracking;
    const char* lockHandTrackingTooltip;
    const char* sectionVirtualControllers;
    const char* virtualControllersDesc;
    const char* virtualControllersRegistered;
    const char* enableVirtualControllers;
    const char* virtCtrlConnected;
    const char* virtCtrlDisconnected;
    const char* virtCtrlConnectTooltip;
    const char* virtCtrlDisconnectTooltip;
    const char* sectionOSC;
    const char* oscDesc;
    const char* enableOSC;
    const char* oscAvatarParam;
    const char* sectionWebSocket;
    const char* wsDesc;
    const char* enableWS;
    const char* wsPortHint;
    const char* wsJsonParam;
    const char* sectionSavedData;
    const char* deleteAllSettings;
    const char* deleteAllDesc;
    const char* deleteConfirmTitle;
    const char* deleteConfirmQ;
    const char* deleteConfirmNote;
    const char* deleteBtn;

    // Tooltips
    const char* tooltipSave;
    const char* tooltipReload;
    const char* tooltipRedirectMode;
    const char* tooltipSmoothCalActive;
    const char* tooltipSmoothCalIdle;
    const char* tooltipQuickCal;
};

// ---------------------------------------------------------------------------
// English
// ---------------------------------------------------------------------------
static const LangStrings k_langEN = {
    "English",
    "\xe2\x97\x8f  Driver connected",
    "\xe2\x97\x8f  Driver not connected",
    "— make sure SteamVR is running with OVRDancers driver installed",
    "Hands: ON", "Hands: OFF",
    "Reload Save", "Save", "  + Add Mapping  ",
    "  Mappings", "  Settings",
    "No mappings yet",
    "Press  + Add Mapping  to begin",
    "-- Waiting for devices to connect --",
    "CONTROLLER", "TRACKER",
    "[ Left Hand ]", "[ Right Hand ]", "[ Hand Tracking ]",
    "Remove",
    "Redirect Mode", "Smooth Cal", "Stop Adjust", "Quick Cal",
    "v  Manual Offset", ">  Manual Offset",
    " ON ", "OFF ",
    "Position (m)", "Rotation (deg)", "Reset pos", "Reset rot", "Reset all",
    "World Offset",
    "  Global position/rotation correction applied after tracker positioning",
    "\xe2\x97\x8f Waiting for devices",
    "Retry",
    "New Device Mapping",
    "Show all devices in both lists",
    "Controller source:",
    "Specific device",
    "Left Hand (auto-switch)",
    "Right Hand (auto-switch)",
    "Select Controller",
    "Select Wrist Tracker",
    "Target Device (will be driven)",
    "Source Device (tracker/driver)",
    "Cancel", "Add Mapping",
    "No controllers or trackers detected. Start SteamVR first.",
    "No trackers detected. Start SteamVR first.",
    "No device holds that hand role right now.\nActivate controllers or hand tracking and try again.",
    "Maps to whichever device currently holds the Left hand role "
        "and auto-switches when hand tracking activates or deactivates.",
    "Maps to whichever device currently holds the Right hand role "
        "and auto-switches when hand tracking activates or deactivates.",
    "Language",
    "About",
    "OVR Dancers Tool  v1.0.0",
    "Source code & issue tracker:",
    "Click to open in browser",
    "Device Visibility",
    "Hide hand trackers",
    "Hides the wrist tracker pucks from the VR view",
    "Controller Bindings",
    "Bind any VR controller button to toggle all hands on/off via SteamVR",
    "Edit Bindings",
    "Opens SteamVR binding editor in headset",
    "Hand Tracking",
    "When Quest hand tracking activates, automatically remaps your mapping to the new "
        "hand tracking device and re-calibrates the position offset.\n"
        "Use 'Left/Right Hand (auto-switch)' in Add Mapping to enable this for a mapping.",
    "Lock hand tracking device (disable auto-switching)",
    "When checked, mappings stay on their current device even if hand tracking\n"
        "activates or deactivates. Use in games that break when input type changes.",
    "Virtual Hand Controllers",
    "Registers virtual hand controllers for users without a physical controller.\n"
        "Map a wrist tracker to VirtCtrl_L or VirtCtrl_R — the tracker becomes your controller.\n"
        "Note: once registered they persist until SteamVR restarts.",
    "Virtual controllers registered in SteamVR.",
    "Enable Virtual Controllers",
    "L: Connected###vcl",
    "L: Disconnected###vcl",
    "Click to soft-disconnect (hides from SteamVR without restarting)",
    "Click to reconnect",
    "OSC Output",
    "Sends OSC UDP messages when hands toggle on/off.\n"
        "Compatible with VRChat, Resonite, and ChilloutVR (CVR) avatar parameters.\n"
        "Default port 9000 is standard for all three.\n"
        "Note: Windows may show a firewall popup — allow access on private networks.",
    "Enable OSC",
    "(avatar parameter name)",
    "WebSocket Output",
    "Runs a WebSocket server that clients connect to for hand toggle events.\n"
        "Resonite can connect as a WebSocket client (ws://127.0.0.1:<port>).\n"
        "Message format: {\"param\":\"<name>\",\"value\":1/0}\n"
        "Note: Windows may show a firewall popup — allow access on private networks.",
    "Enable WebSocket Server",
    "(default 8080, connect via ws://127.0.0.1:<port>)",
    "(parameter name in JSON message)",
    "Saved Data",
    "Delete All Settings",
    "Clears all saved mappings and offsets",
    "Confirm Delete###del_confirm",
    "Delete all saved mappings?",
    "This cannot be undone.",
    "Delete",
    "Save all current mappings to disk",
    "Restore mappings from last save file",
    "Redirect Mode: drives controller from tracker's\npose callbacks directly (OVRIE method).\n"
        "May work better when mixing headsets and\ncontrollers from different manufacturers\n"
        "(e.g. Quest headset with Index controllers).",
    "Adjusting live — move the tracker to shift the virtual\n"
        "controller position. Press to stop and keep current offset.",
    "Real-time position adjust: press to start, move the tracker\n"
        "to where the virtual controller should be, press again to stop.\n"
        "Useful for fine-tuning Redirect Mode offset.",
    "Disables mapping briefly, reads the real\ncontroller position, then re-enables.",
};

// ---------------------------------------------------------------------------
// Japanese  (source is UTF-8; compiled with /utf-8 on MSVC)
// ---------------------------------------------------------------------------
static const LangStrings k_langJA = {
    u8"日本語",
    u8"●  ドライバー接続済み",
    u8"●  ドライバー未接続",
    u8"— SteamVRがOVRDancersドライバーと共に起動していることを確認してください",
    u8"手: ON", u8"手: OFF",
    u8"再読み込み", u8"保存",
    u8"  + マッピング追加  ",
    u8"  マッピング", u8"  設定",
    u8"マッピングなし",
    u8"+ マッピング追加 を押して開始",
    u8"-- デバイスの接続を待っています --",
    u8"コントローラー", u8"トラッカー",
    u8"[ 左手 ]", u8"[ 右手 ]", u8"[ ハンドトラッキング ]",
    u8"削除",
    u8"リダイレクトモード",
    u8"スムースキャル", u8"調整停止",
    u8"クイックキャル",
    u8"v  マニュアルオフセット",
    u8">  マニュアルオフセット",
    u8" ON ", u8"OFF ",
    u8"位置 (m)", u8"回転 (度)",
    u8"位置リセット", u8"回転リセット",
    u8"全リセット",
    u8"ワールドオフセット",
    u8"  トラッカー配置後に適用されるグローバル位置/回転補正",
    u8"● デバイス待機中",
    u8"再試行",
    u8"新規デバイスマッピング",
    u8"両リストに全デバイスを表示",
    u8"コントローラーソース:",
    u8"特定のデバイス",
    u8"左手 (自動切替)",
    u8"右手 (自動切替)",
    u8"コントローラーを選択",
    u8"リストラッカーを選択",
    u8"ターゲットデバイス (制御対象)",
    u8"ソースデバイス (トラッカー/ドライバー)",
    u8"キャンセル", u8"マッピング追加",
    u8"コントローラーまたはトラッカーが検出されません。先にSteamVRを起動してください。",
    u8"トラッカーが検出されません。先にSteamVRを起動してください。",
    u8"現在そのboard roleを持つデバイスがありません。\nコントローラーまたはハンドトラッキングを有効にして再試行してください。",
    u8"現在左手ロールを持つデバイスにマップし、ハンドトラッキングの切替時に自動切替します。",
    u8"現在右手ロールを持つデバイスにマップし、ハンドトラッキングの切替時に自動切替します。",
    u8"言語",
    u8"アバウト",
    u8"OVR Dancers Tool  v1.0.0",
    u8"ソースコード & イシュートラッカー:",
    u8"クリックしてブラウザで開く",
    u8"デバイス表示",
    u8"ハンドトラッカーを非表示",
    u8"VRビューからリストラッカーを非表示にします",
    u8"コントローラーバインド",
    u8"SteamVR経由でVRコントローラーボタンに手のオン/オフを割り当て",
    u8"バインド編集",
    u8"ヘッドセット内でSteamVRバインドエディターを開きます",
    u8"ハンドトラッキング",
    u8"Questハンドトラッキングが有効になると、マッピングを新しいハンドトラッキングデバイスに自動的に切り替えて位置オフセットを再キャリブレーションします。\nマッピング追加で「左手/右手(自動切替)」を使用して有効にしてください。",
    u8"ハンドトラッキングデバイスをロック (自動切替を無効化)",
    u8"チェック時、ハンドトラッキングが切り替わってもマッピングは現在のデバイスに留まります。",
    u8"バーチャルハンドコントローラー",
    u8"物理コントローラーを持たないユーザー向けに仮想ハンドコントローラーを登録します。\nリストラッカーをVirtCtrl_LまたはVirtCtrl_Rにマップするとトラッカーがコントローラーになります。\n注意: 登録後SteamVR再起動まで有効のままです。",
    u8"仮想コントローラーをSteamVRに登録済み。",
    u8"バーチャルコントローラーを有効化",
    u8"L: 接続中###vcl",
    u8"L: 未接続###vcl",
    u8"ソフト切断するにはClick (SteamVRを再起動せず非表示になります)",
    u8"再接続するにはClick",
    u8"OSC出力",
    u8"OSC UDPメッセージを送信します。\n"
     u8"VRChat、Resonite、ChilloutVRのアバターパラメーターに対応。\n"
     u8"デフォルトポート" " 9000 " u8"は3つ全てで標準。\n"
     u8"注意: Windowsでファイアウォールポップアップが表示された場合はプライベートネットワークへのアクセスを許可してください。",
    u8"OSCを有効化",
    u8"(アバターパラメーター名)",
    u8"WebSocket出力",
    u8"WebSocketサーバーを起動してクライアントが接続します。\n"
     u8"Resoniteはws://127.0.0.1:<port>で接続可能。\n"
     u8"メッセージ形式: {\"param\":\"<name>\",\"value\":1/0}\n"
     u8"注意: Windowsでファイアウォールポップアップが表示された場合はプライベートネットワークへのアクセスを許可してください。",
    u8"WebSocketサーバーを有効化",
    u8"(デフォルト" " 8080, ws://127.0.0.1:<port>" u8"で接続)",
    u8"(JSONメッセージのパラメーター名)",
    u8"保存データ",
    u8"全設定を削除",
    u8"保存済みのマッピングとオフセットを全て消去します",
    u8"削除の確認###del_confirm",
    u8"保存済みのマッピングを全て削除しますか？",
    u8"この操作は元に戻せません。",
    u8"削除",
    u8"現在のマッピングをディスクに保存",
    u8"最後の保存ファイルから復元",
    u8"リダイレクトモード: トラッカーのポーズコールバックから直接コントローラーを駆動します。",
    u8"調整中 - トラッカーを動かして位置を調整。停止するにはボタンを押してください。",
    u8"トラッカーを動かして位置をリアルタイム調整します。",
    u8"マッピングを一時無効にし、実のコントローラー位置を読み取って再有効化します。",
};

// ---------------------------------------------------------------------------
// Korean  (source is UTF-8; compiled with /utf-8 on MSVC)
// ---------------------------------------------------------------------------
static const LangStrings k_langKO = {
    u8"한국어",
    u8"●  드라이버 연결됨",
    u8"●  드라이버 연결 안 됨",
    u8"— OVRDancers 드라이버와 함께 SteamVR이 실행 중인지 확인하세요",
    u8"손: ON", u8"손: OFF",
    u8"다시 불러오기", u8"저장",
    u8"  + 매핑 추가  ",
    u8"  매핑", u8"  설정",
    u8"매핑 없음",
    u8"+ 매핑 추가 를 눈러 시작",
    u8"-- 기기 연결 대기 중 --",
    u8"컨트롤러", u8"트래커",
    u8"[ 왼손 ]", u8"[ 오른손 ]", u8"[ 핸드 트래킹 ]",
    u8"제거",
    u8"리다이렉트 모드",
    u8"스무스 캘", u8"조정 중지",
    u8"핀 캘",
    u8"v  수동 오프셋",
    u8">  수동 오프셋",
    u8" ON ", u8"OFF ",
    u8"위치 (m)", u8"회전 (도)",
    u8"위치 리셋", u8"회전 리셋",
    u8"전체 리셋",
    u8"월드 오프셋",
    u8"  트래커 배치 후 적용되는 글로벌 위치/회전 보정",
    u8"● 기기 대기 중",
    u8"재시도",
    u8"새 기기 매핑",
    u8"두 목록에 모든 기기 표시",
    u8"컨트롤러 소스:",
    u8"특정 기기",
    u8"왼손 (자동 전환)",
    u8"오른손 (자동 전환)",
    u8"컨트롤러 선택",
    u8"손목 트래커 선택",
    u8"대상 기기 (제어될 기기)",
    u8"소스 기기 (트래커/드라이버)",
    u8"취소", u8"매핑 추가",
    u8"컨트롤러 또는 트래커가 감지되지 않았습니다. 먼저 SteamVR을 실행하세요.",
    u8"트래커가 감지되지 않았습니다. 먼저 SteamVR을 실행하세요.",
    u8"현재 해당 손 역할을 가진 기기가 없습니다.\n컨트롤러 또는 핸드 트래킹을 활성화하고 다시 시도하세요.",
    u8"현재 왼손 역할을 가진 기기에 매핑합니다.",
    u8"현재 오른손 역할을 가진 기기에 매핑합니다.",
    u8"언어",
    u8"정보",
    u8"OVR Dancers Tool  v1.0.0",
    u8"소스 코드 & 이슈 트래커:",
    u8"클릭하여 브라우저에서 열기",
    u8"기기 표시",
    u8"손 트래커 숨기기",
    u8"VR 뷰에서 손목 트래커 펃을 숨깁니다",
    u8"컨트롤러 바인딩",
    u8"SteamVR을 통해 VR 컨트롤러 버튼에 손 켜기/끄기 바인딩",
    u8"바인딩 편집",
    u8"헤드셋에서 SteamVR 바인딩 편집기를 엽니다",
    u8"핸드 트래킹",
    u8"Quest 핸드 트래킹이 활성화되면 매핑을 새 핸드 트래킹 기기로 자동 변경하고 위치 오프셋을 재보정합니다.\n"
     u8"매핑 추가에서 '왼손/오른손(자동 전환)'을 사용하세요.",
    u8"핸드 트래킹 기기 잠금 (자동 전환 비활성화)",
    u8"체크 시 핸드 트래킹이 전환되어도 매핑이 현재 기기에 유지됩니다.",
    u8"가상 손 컨트롤러",
    u8"물리적 컨트롤러가 없는 사용자를 위한 가상 손 컨트롤러를 등록합니다.\n"
     u8"손목 트래커를 VirtCtrl_L 또는 VirtCtrl_R에 매핑하면 트래커가 컨트롤러가 됩니다.\n"
     u8"참고: 등록 후 SteamVR 재시작 전까지 유지됩니다.",
    u8"가상 컨트롤러가 SteamVR에 등록되었습니다.",
    u8"가상 컨트롤러 활성화",
    u8"L: 연결됨###vcl",
    u8"L: 연결 안 됨###vcl",
    u8"소프트 끝기 (SteamVR 재시작 없이 숨깁니다)",
    u8"재연결하려면 클릭",
    u8"OSC 출력",
    u8"손 켜기/끄기 시 OSC UDP 메시지를 전송합니다.\n"
     u8"VRChat, Resonite, ChilloutVR 아바타 파라미터와 호환.\n"
     u8"기본 포트 9000은 세 플랫폼 모두 표준입니다.\n"
     u8"참고: Windows에서 방화벽 팝업이 표시되면 개인 네트워크의 액세스를 허용하세요.",
    u8"OSC 활성화",
    u8"(아바타 파라미터 이름)",
    u8"WebSocket 출력",
    u8"WebSocket 서버를 실행하여 클라이언트가 접속합니다.\n"
     u8"Resonite는 ws://127.0.0.1:<port>로 접속 가능합니다.\n"
     u8"메시지 형식: {\"param\":\"<name>\",\"value\":1/0}\n"
     u8"참고: Windows에서 방화벽 팝업이 표시되면 개인 네트워크의 액세스를 허용하세요.",
    u8"WebSocket 서버 활성화",
    u8"(기본 8080, ws://127.0.0.1:<port>로 접속)",
    u8"(JSON 메시지의 파라미터 이름)",
    u8"저장된 데이터",
    u8"모든 설정 삭제",
    u8"저장된 모든 매핑과 오프셋을 지웁니다",
    u8"삭제 확인###del_confirm",
    u8"저장된 모든 매핑을 삭제하시겠습니까?",
    u8"이 작업은 취소할 수 없습니다.",
    u8"삭제",
    u8"현재 매핑을 디스크에 저장",
    u8"마지막 저장 파일에서 복원",
    u8"리다이렉트 모드: 트래커의 포즈 콜백에서 직접 컨트롤러를 제어합니다.",
    u8"조정 중 - 트래커를 이동하여 위치를 조정하세요. 중지하려면 버튼을 누르세요.",
    u8"트래커를 이동하여 위치를 실시간 조정합니다.",
    u8"매핑을 잃시 비활성화하여 실제 컨트롤러 위치를 읽어 재활성화합니다.",
};

// ---------------------------------------------------------------------------
// Language table — indexed by Language enum
// ---------------------------------------------------------------------------
static const LangStrings* const k_langTable[LANG_COUNT] = {
    &k_langEN,
    &k_langJA,
    &k_langKO,
};

inline Language g_language = LANG_EN;
inline const LangStrings& TR() { return *k_langTable[g_language]; }
