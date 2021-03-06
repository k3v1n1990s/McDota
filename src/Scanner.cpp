#include "Scanner.h"

#include <cstdint>

#include "Interfaces.h"
#include "Utils/Patternfinder.h"

bool Scanner::FindGlobalVars() {

	// CSource2Client::SetGlobals()
	// 55                         push    rbp
	// 48 89 E5                   mov     rbp, rsp
	// 53                         push    rbx
	// 48 83 EC 08                sub     rsp, 8
	// 48 85 F6                   test    rsi, rsi
	// 48 8B 1D 1D 30 3A 03       mov     rbx, cs:_gpGlobals

    void ( CSource2Client::*setGlobalsPtr )( void* ) = &CSource2Client::SetGlobals;

    uintptr_t SetGlobals = reinterpret_cast<uintptr_t>( (void*)(client->*setGlobalsPtr) );

	globalVars = **reinterpret_cast<CGlobalVars***>(GetAbsoluteAddress(SetGlobals + 9, 3, 7));
	return true;
}

bool Scanner::FindGameEntitySystem()
{
    // CPrediction::ReinitPredictables()
    // 55 48 89 E5 41 56 49 89 FE 41 55 4C 8B
    // Look for string "ReinitPredictables".
    // This function contains both a pointer to CGameEntitySystem and a call to CGameEntitySystem::GetHighestEntityIndex();
    // 55                      push    rbp
    // 48 89 E5                mov     rbp, rsp
    // 41 56                   push    r14
    // 49 89 FE                mov     r14, rdi
    // 41 55                   push    r13
    // 4C 8B 2D 7E 95 31 03    mov     r13, cs:_g_pGameEntitySystem
    // 41 54                   push    r12
    // 53                      push    rbx
    // 31 DB                   xor     ebx, ebx
    // 49 8B 7D 00             mov     rdi, [r13+0]
    // E8 60 DF DD FF          call    CGameEntitySystem__GetHighestEntityIndex

    uintptr_t reinitPredictables = PatternFinder::FindPatternInModule("libclient.so", (unsigned char*) "\x55\x48\x89\xE5\x41\x56\x49\x89\xFE\x41\x55\x4C\x8B", "xxxxxxxxxxxxx", "reinitPredictables");

	if( !reinitPredictables ){
        MC_PRINTF_ERROR("reinitPredictables sig failed\n");
        return false;
    }

    uintptr_t jumpInstructionAddr = reinitPredictables + 27;
    int32_t jumpDisplacement = *reinterpret_cast<int32_t*>(jumpInstructionAddr + 1);
    uintptr_t getHighestEntIndexFnAddr = (jumpInstructionAddr + 5) + jumpDisplacement;

    // CGameEntitySystem::GetHighestEntityIndex()
    // We get the class offset for the highest Ent Index
    // 55                      push    rbp
    // 8B 87 60 A0 01 00       mov     eax, dword ptr ds:stru_1A060.r_offset[rdi]
    // 48 89 E5                mov     rbp, rsp
    // 5D                      pop     rbp

    highestEntIndexOffset = *reinterpret_cast<uint32_t*>(getHighestEntIndexFnAddr + 3);

    entitySystem = **reinterpret_cast<CGameEntitySystem***>( GetAbsoluteAddress(reinitPredictables + 11, 3, 7) );

	return true;
}


bool Scanner::FindVScript()
{
	// Shortly after "__ReplaceClosures"
	// 48 8B 3D ?? ?? ?? ?? 0F B6 D3
	// 0F 84 F5 00 00 00                       jz      loc_2A31390
	// 48 8B 3D 16 38 A3 03                    mov     rdi, cs:qword_6464AB8
	// 0F B6 D3                                movzx   edx, bl
	// 4C 89 E6                                mov     rsi, r12

	uintptr_t initialLine = PatternFinder::FindPatternInModule("libclient.so", (unsigned char*)"\x48\x8B\x3D\x00\x00\x00\x00"
			                                                                                    "\x0F\xB6\xD3", "xxx????xxx", "FindVScript");

	if( !initialLine ){
        MC_PRINTF_ERROR("ERROR VScript sig failed\n");
        return false;
    }

    // The compiler did some weird shit here to morph 2 vtables into one.
    // Just need to decrease thisptr by 0x10 to get to bigger vtable. (IGameSystem + CVScriptGameSystem) instead of just (CVScriptGameSystem).
	void* scriptSys = *reinterpret_cast<CVScriptGameSystem**>( GetAbsoluteAddress(initialLine, 3, 7) );
    scriptSys = reinterpret_cast<char*>(scriptSys) - 0x10;
    vscriptSystem = (CVScriptGameSystem*)scriptSys;

	return true;
}

bool Scanner::FindCNetworkMessages()
{
	// Start of function containing "net_validatemessages"
	// 48 8D 05 ?? ?? ?? ?? 48 89 E5 41 57 41 56 48 8D 0D
	// 48 8D 05 43 13 4C 00    lea     rax, CNetworkMessages <------------
    // 48 89 E5                mov     rbp, rsp
    // 41 57                   push    r15
    // 41 56                   push    r14
    // 48 8D 0D C5 D4 1D 00    lea     rcx, aActivatesDeact
 
	uintptr_t func = PatternFinder::FindPatternInModule("libnetworksystem.so", (unsigned char*)"\x48\x8D\x05\x00\x00\x00\x00"
                                                                                                "\x48\x89\xE5"
                                                                                                "\x41\x57"
                                                                                                "\x41\x56"
                                                                                                "\x48\x8D\x0D", "xxx????xxxxxxxxxx" "FindCNetworkMessages");
	if( !func ){
        MC_PRINTF_ERROR("FindCNetworkMessages sig failed\n");
        return false;
    }

    static void* refHack = reinterpret_cast<CNetworkMessages*>( GetAbsoluteAddress( func, 3, 7 ) );
	networkMessages = (CNetworkMessages*)&refHack;
	return true;
}


bool Scanner::FindPanelArrayOffset()
{
	unsigned int knownGoodOffset = 0x1C0;
	//  CUIEngine::IsValidPanelPointer()
	//
	//   55                      push    rbp
	//   48 81 C7 B8 01 00 00    add     rdi, 1B8h <--------

    bool ( panorama::UIEngine::*isValidPanelPtrPtr )( const panorama::IUIPanel* ) = &panorama::UIEngine::IsValidPanelPointer;

    uintptr_t IsValidPanelPointer = reinterpret_cast<uintptr_t>( (void*)(panoramaEngine->AccessUIEngine()->*isValidPanelPtrPtr) );
	panorama::panelArrayOffset = *(unsigned int*)(IsValidPanelPointer + 19);

	if( panorama::panelArrayOffset != knownGoodOffset ) {
		MC_PRINTF_WARN( "Array Offset Changed. Is (%x), supposed to be (%x)\n", panorama::panelArrayOffset, knownGoodOffset );
	}
	if( panorama::panelArrayOffset > 0x300 || panorama::panelArrayOffset < 0x100 ){
		MC_PRINTF_ERROR("Array Offset Drastic Change. Is (%x)!\n", panorama::panelArrayOffset );
		return false;
	}
	return true;
}

bool Scanner::FindViewRender()
{
    //  InitGameSystems()
    // 55                      push    rbp
    // 48 89 E5                mov     rbp, rsp
    // 41 54                   push    r12
    // 53                      push    rbx
    // 48 83 EC 20             sub     rsp, 20h
    // E8 30 2C 71 00          call    GetViewRenderInstance <-------------
	uintptr_t initGameSystemsFunc = PatternFinder::FindPatternInModule("libclient.so", (unsigned char*)"\x55\x48\x89\xE5\x41\x54\x53\x48\x83\xEC"
			                                                            "\x00" // ??
			                                                            "\xE8"
			                                                            "\x00\x00\x00\x00" // ??
			                                                            "\x48\x8B\x15", "xxxxxxxxxx?x????xxx", "GetViewRender Function");
    if( !initGameSystemsFunc ){
        MC_PRINTF_ERROR("initGameSystemsFunc sig failed\n");
        return false;
    }
	typedef CViewRender* (* GetViewRenderFn)();
    GetViewRenderFn GetViewRender = reinterpret_cast<GetViewRenderFn>(GetAbsoluteAddress(initGameSystemsFunc + 11, 1, 5));
    viewRender = GetViewRender();
    return true;
}

bool Scanner::FindClientMode()
{
	// CSource2Client::HudUpdate()
	// 55                      push    rbp
	// 48 89 E5                mov     rbp, rsp
	// 53                      push    rbx
	// 89 F3                   mov     ebx, esi
	// 48 83 EC 18             sub     rsp, 18h
	// E8 30 E5 6E 01          call    sub_44B48D0
	// E8 8B 2C ED FF          call    sub_2C99030
	// 84 C0                   test    al, al
	// 75 37                   jnz     short loc_2DC63E0
	// 31 FF                   xor     edi, edi
	// E8 E0 53 20 00          call    GetClientMode <----------------

    void ( CSource2Client::*hudUpdatePtr )( bool ) = &CSource2Client::HudUpdate;

	uintptr_t hudUpdateFn = reinterpret_cast<uintptr_t>( (void*)(client->*hudUpdatePtr) );

	if( !hudUpdateFn ){
		MC_PRINTF_ERROR("hudUpdateFn is NULL\n");
		return false;
	}
	typedef IClientMode* (*GetClientModeFn) ( int splitScreenslot );
	GetClientModeFn GetClientMode = reinterpret_cast<GetClientModeFn>( GetAbsoluteAddress( hudUpdateFn + 27, 1, 5 ) );

	if( !GetClientMode ){
		MC_PRINTF_ERROR("GetClientMode function is NULL\n");
		return false;
	}

	clientMode = GetClientMode( 0 );
	if( !clientMode ){
		MC_PRINTF_ERROR("GetClientMode function failed to return ClientMode\n");
		return false;
	}

	return true;
}

bool Scanner::FindCamera()
{
	// CenterOnLocalPlayersHero(), xref "CMD_SelectHeroStart" and look around there
	// E8 ?? ?? ?? ?? F3 0F 10 ?? ?? 31 C9 31 D2
	// E8 E5 84 09 00          call    GDOTADefaultCamera
	// F3 0F 10 4D EC          movss   xmm1, [rbp+var_14]
	// 31 C9                   xor     ecx, ecx
	// 31 D2                   xor     edx, edx
	

	uintptr_t getCameraFuncAddr = PatternFinder::FindPatternInModule("libclient.so", (unsigned char*)"\xE8\x00\x00\x00\x00"
			                                                                                          "\xF3\x0F\x10\x00\x00"
			                                                                                          "\x31\xC9"
			                                                                                          "\x31\xD2", "x????xxx??xxxx", "Get DOTADefaultCamera");

	if( !getCameraFuncAddr ){
		MC_PRINTF_ERROR("Get Camera sig failed\n");
		return false;
	}

	GetCurrentCamera = reinterpret_cast<GetCameraFn>( GetAbsoluteAddress(getCameraFuncAddr, 1, 5) );

	camera = GetCurrentCamera();
	return true;
}


bool Scanner::FindGameEventManager()
{
    void ( CSource2Client::*playerInfoChangedPtr )( int ) = &CSource2Client::PlayerInfoChanged;

    uintptr_t playerInfoChangedFn = reinterpret_cast<uintptr_t>( (void*)(client->*playerInfoChangedPtr) );


    uintptr_t gameEventManagerAddr = reinterpret_cast<uintptr_t>( GetAbsoluteAddress(playerInfoChangedFn + 52, 3, 7) );
    if( !gameEventManagerAddr ){
        MC_PRINTF_ERROR("Game Event Manager Addr sig broke!\n");
        return false;
    }

    gameEventManager = **reinterpret_cast<CGameEventManager ***>( gameEventManagerAddr );

    if( !gameEventManager ){
        MC_PRINTF_ERROR("Game Event Manager is NULL\n");
        return false;
    }
    return true;
}

bool Scanner::FindDBPlayPanel()
{
	// 48 8B 05 ?? ?? ?? ?? BE 07 00 00 00
	// Right below "disconnect clicked disconnect button"
	// 48 8B 05 E7 99 0A 03    mov     rax, cs:_g_pDBPlayPanel
	// BE 07 00 00 00          mov     esi, 7
	
	uintptr_t playPanelLine = PatternFinder::FindPatternInModule("libclient.so", (unsigned char*)"\x48\x8B\x05\x00\x00\x00\x00"
			                                                                                      "\xBE\x07\x00\x00\x00", "xxx????xxxxx", "DBPlay Panel");

	if( !playPanelLine ){
		MC_PRINTF_ERROR("DBPlayPanel Sig is broke!\n");
		return false;
	}

	uintptr_t playPanelAddr = reinterpret_cast<uintptr_t>( GetAbsoluteAddress( playPanelLine, 3, 7 ) );

	if( !playPanelAddr ){
		MC_PRINTF_ERROR("DBPlayPanelAddr is NULL\n");
		return false;
	}

	gDBPlayPanel = **reinterpret_cast<panorama::CPanel2D***>( playPanelAddr );

	return true;
}

bool Scanner::FindSoundOpSystem()
{
    // 48 8B 1D ?? ?? ?? ?? C6 85 ?? ?? ?? ?? ?? 64 48 8B 04 25 ?? ?? ?? ?? 48 89 ?? ?? 31 C0 C7
    // xref "DOTAMusic.MainLoop" to first function ( it should also have "opvars" and "dota_music_opvars", but NOT "current_music") go to start of function; it is the first cs:xxxxxxxx address
    // 48 8B 1D 53 97 20 03          mov     rbx, cs:off_61D92E0
	// C6 85 90 FE FF FF FF          mov     [rbp+var_170], 0FFh
	// 64 48 8B 04 25 28 00 00 00    mov     rax, fs:28h
	// 48 89 45 E8                   mov     [rbp+var_18], rax
	// 31 C0                         xor     eax, eax
	// C7 85 98 FE FF FF 00 00 00 00 mov     [rbp+var_168], 0
    
    uintptr_t soundOpSystemLine = PatternFinder::FindPatternInModule("libclient.so", (unsigned char*)"\x48\x8B\x1D\x00\x00\x00\x00"
			                                                                                          "\xC6\x85\x00\x00\x00\x00\x00"
			                                                                                          "\x64\x48\x8B\x04\x25\x00\x00\x00\x00"
			                                                                                          "\x48\x89\x00\x00"
			                                                                                          "\x31\xC0"
			                                                                                          "\xC7", "xxx????xx?????xxxxx????xx??xxx" "SoundOpSystem");

    if( !soundOpSystemLine ){
        MC_PRINTF_ERROR("SoundOpSystem Sig is broke!\n");
        return false;
    }

    uintptr_t soundOpSystemAddr = reinterpret_cast<uintptr_t>( GetAbsoluteAddress( soundOpSystemLine, 3, 7 ) );

    if( !soundOpSystemAddr ){
        MC_PRINTF_ERROR("SoundOpSystemAddr is NULL!\n");
        return false;
    }

    soundOpSystem = **reinterpret_cast<CSoundOpSystem***>( soundOpSystemAddr );

    return true;
}


bool Scanner::FindAcceptMatch()
{
	// xref "ui.click_back"
	// 55 48 89 E5 53 89 F3 48 83 EC ?? E8 ?? ?? ?? ?? 0F B6 F3 48 89
	// CDOTA_DB_Play__OnAcceptMatch proc near
	// 55                      push    rbp
	// 48 89 E5                mov     rbp, rsp
	// 53                      push    rbx
	// 89 F3                   mov     ebx, esi
	// 48 83 EC 08             sub     rsp, 8
	// E8 00 83 9F FF          call    GDOTAGCClientSystem
	// 0F B6 F3                movzx   esi, bl
	// 48 89 C7                mov     rdi, rax

	uintptr_t funcAddr = PatternFinder::FindPatternInModule("libclient.so", (unsigned char*)"\x55"
			                                                                                "\x48\x89\xE5"
			                                                                                "\x53"
			                                                                                "\x89\xF3"
			                                                                                "\x48\x83\xEC\x00"
			                                                                                "\xE8\x00\x00\x00\x00"
			                                                                                "\x0F\xB6\xF3"
			                                                                                "\x48\x89", "xxxxxxxxxx?x????xxxxx", "On Accept Match");
	if( !funcAddr ){
		MC_PRINTF_ERROR("On Accept Match Sig is broke!\n");
		return false;
	}
	OnAcceptMatch = reinterpret_cast<OnAcceptMatchFn>( funcAddr );
	return true;
}


// I could probably calculate this myself but I'm missing the world matrix, engine->WorldToScreenMatrix seems to be gone in Source 2
// Using this function is ez
bool Scanner::FindWorldToScreen()
{
	// Xref "WorldToScreenX" to the rdi one. go to the function above it. this is the only function called in That function.
	// 55 48 89 E5 41 54 49 89 FC 53 48 89 F3 48 83 EC ?? 48 85 D2
	// GetVectorInScreenSpace()
	// 55                      push    rbp
	// 48 89 E5                mov     rbp, rsp
	// 41 54                   push    r12
	// 49 89 FC                mov     r12, rdi
	// 53                      push    rbx
	// 48 89 F3                mov     rbx, rsi
	// 48 83 EC 50             sub     rsp, 50h
	// 48 85 D2                test    rdx, rdx

	uintptr_t funcAddr = PatternFinder::FindPatternInModule("libclient.so", (unsigned char*)"\x55"
			                                                                                "\x48\x89\xE5"
			                                                                                "\x41\x54"
			                                                                                "\x49\x89\xFC"
			                                                                                "\x53"
			                                                                                "\x48\x89\xF3"
			                                                                                "\x48\x83\xEC\x00"
			                                                                                "\x48\x85\xD2", "xxxxxxxxxxxxxxxx?xxx", "WorldToScreen (GetVectorInScreenSpace)");
	if( !funcAddr ){
		MC_PRINTF_ERROR("On WorldToScreen sig is broke!\n");
		return false;
	}

	GetVectorInScreenSpace = reinterpret_cast<GetVectorInScreenSpaceFn>( funcAddr );
	return true;
}


bool Scanner::FindRichPresence()
{
	// CSource2Client::NotifyClientSignon()
	// 55                      push    rbp
	// 48 89 E5                mov     rbp, rsp
	// 41 54                   push    r12
	// 44 0F B7 E6             movzx   r12d, si
	// 53                      push    rbx
	// 89 F3                   mov     ebx, esi
	// 48 83 EC 10             sub     rsp, 10h
	// E8 8A 85 73 00          call    GDOTAGCClientSystem
	// 44 89 E6                mov     esi, r12d
	// 48 89 C7                mov     rdi, rax
	// E8 0F 20 74 00          call    GDOTAGCClientSystem__NotifyClientSignon(int)
	// E8 7A 85 73 00          call    GDOTAGCClientSystem
	// 89 DA                   mov     edx, ebx
	// C1 EA 10                shr     edx, 10h
	// 83 E2 01                and     edx, 1
	// 88 90 FC 05 00 00       mov     [rax+5FCh], dl
	// E8 17 56 46 00          call    RichPresence   <----------------

	void ( CSource2Client::*notifyClientSignonPtr )( int ) = &CSource2Client::NotifyClientSignon;
	uintptr_t NotifyClientSignon = reinterpret_cast<uintptr_t>( (void*)(client->*notifyClientSignonPtr) );

	typedef CDOTARichPresence* ( *GetRichPresenceFn )();
	GetRichPresenceFn GetRichPresence = reinterpret_cast<GetRichPresenceFn>( GetAbsoluteAddress(NotifyClientSignon + 52, 1, 5) );

	richPresence = GetRichPresence();


	// xref "active RP" to SetRPStatus()
	// 55 48 89 E5 41 57 41 56 41 89 D6 41 55 41 54 49 89 FC 53 48 81
	uintptr_t funcAddr = PatternFinder::FindPatternInModule( "libclient.so",
															 ( unsigned char* )"\x55\x48\x89\xE5\x41\x57\x41\x56\x41\x89\xD6\x41\x55\x41\x54\x49\x89\xFC\x53\x48\x81",
															 "xxxxxxxxxxxxxxxxxxxxx", "SetRPStatus" );

	if( !funcAddr ){
		MC_PRINTF_ERROR("SetRPStatus sig is broke!\n");
		return false;
	}

	SetRPStatus = reinterpret_cast<SetRPStatusFn>( funcAddr );
	return true;
}




















