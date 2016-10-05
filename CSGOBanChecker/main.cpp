#include <iostream>
#include <vector>
#include <cinttypes>
#include <unordered_map>
#include <sstream>
#include <string>
#include <fstream>
#include <streambuf>
#include <algorithm>
#include <ctime>

#include <Windows.h>
#include <tchar.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <Shlobj.h>
#include <strsafe.h>
#include "resource.h"


#include "CSGOSimpleDemoUsersInfo.h"
#include "json.hpp"

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Urlmon.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
"version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using json = nlohmann::json;

// TODO(Pedro): Add num games parsed

enum PROCESS_ACTIONS {
	WAIT = 0,
	PROCESS
};

static HWND MAIN_WINDOW;

struct PlayerInfoSimple {
	char name[500];
	char demoFile[500];
};

struct BANINFO {
	std::string steamID;
	bool communityBanned;
	bool VACBanned;
	uint32_t numberOfVACBans;
	uint32_t daysSinceLastBan;
	uint32_t numberOfGameBans;
	std::string economyBan;
};

static std::string DEFAULT_BROWSER;
static std::fstream CONFIG_FILE;
static int PROCESS_ACTION = 0;
static bool CLOSE_APP = false;
static std::string STEAM_FOLDER;
static bool IS_STEAM_INSTALLLED = false;
static bool DEFAULT_CSGO_REPLAY_FOLDER_EXISTS = false;
static std::string DEFAULT_CSGO_REPLAY_FOLDER;
static std::string DEMO_FOLDER;
static std::vector<std::string> DEMOS;
static HWND PROGRESS_BAR;
static std::unordered_map<uint64_t, PlayerInfoSimple> PLAYERS_ID;
static std::vector<BANINFO> PLAYERS_BANNED;
static float PROGRESS_STEP;
static float PROGRESS;
static int TOTAL_DEMOS;

const std::string demosParsedFile = "C:\\CSGOVacChecker\\demos_parsed.txt";
const std::string playersParsedFile = "C:\\CSGOVacChecker\\players.bin";

const std::string STEAM_API_KEY = "B01A333B1E01E496DB46E9416E7594CC";
const std::string STEAM_API_CALL = "http://api.steampowered.com/ISteamUser/GetPlayerBans/v1/?key=" + STEAM_API_KEY + "&steamids=";
const std::string DEFAULT_DOWNLOAD_LOCATION = "C:\\CSGOVacChecker\\";
static std::vector<std::string> LIST_GET_REQUESTS;

static std::string PLAY_DEMO = "steam://rungame/730/76561202255233023/+playdemo%20";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void LoadFileInResource(int name, int type, DWORD& size, const char*& data);

std::string getWindowsLikePathString(std::string path);

bool compareByLastBan(const BANINFO &a, const BANINFO &b);

int CALLBACK browseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);

std::string convertSpaceToP20(const std::string &string);

std::string browseFolder(std::string saved_path, std::string title);

void workerThread();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR cmdLine, int cmdShow) {
	MSG msg;
	int screenX;
	int screenY;
	int sizeX;
	int sizeY;
	HANDLE threadHandle;

	WNDCLASSEX wndClass = {};
	wndClass.cbSize = sizeof(wndClass);
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = WndProc;
	wndClass.hInstance = hInstance;
	wndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wndClass.lpszClassName = "CSGOVacChecker";
	wndClass.hCursor = (HCURSOR)LoadImage(hInstance, MAKEINTRESOURCE(IDC_ARROW), IMAGE_CURSOR, 0, 0, LR_SHARED);
	wndClass.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
	wndClass.hIconSm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);

	RegisterClassEx(&wndClass);

	screenX = GetSystemMetrics(SM_CXSCREEN);
	screenY = GetSystemMetrics(SM_CYSCREEN);

	sizeX = 500;
	sizeY = 60;

	MAIN_WINDOW = CreateWindowEx(0,
		"CSGOVacChecker",  "CSGOVacChecker - Pedrohenrique.ninja",
		WS_SYSMENU|WS_MINIMIZEBOX|WS_OVERLAPPED, (screenX - sizeX) / 2, (screenY - sizeY) / 2, 
		sizeX, sizeY, 0, 0, hInstance, 0);

	//ShowWindow(hwnd, cmdShow);
	UpdateWindow(MAIN_WINDOW);

	threadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)workerThread, NULL, 0, NULL);

	while (true) {
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	UnregisterClass("CSGOVacChecker", hInstance);
	WaitForSingleObject(threadHandle, INFINITE);

	return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CLOSE:
		{
			DestroyWindow(MAIN_WINDOW);
		} break;
		case WM_DESTROY:
		{
			CONFIG_FILE.close();
			CLOSE_APP = true;
			PostQuitMessage(0);
		} break;
		case WM_CREATE: {
			// check if there is a config file
			CreateDirectory(DEFAULT_DOWNLOAD_LOCATION.c_str(), NULL);

			std::string configFile = "C:\\CSGOVacChecker\\config.txt";
			CONFIG_FILE.open(configFile.c_str(), std::ios::in);

			bool hasConfig = false;

			if (CONFIG_FILE.is_open()) {
				std::string checkFileHeader;
				std::getline(CONFIG_FILE, checkFileHeader);

				if (checkFileHeader == "CSGOVacCheckerConfigFile") {
					hasConfig = true;
					std::string tmp;
					std::getline(CONFIG_FILE, tmp);
					IS_STEAM_INSTALLLED = (tmp == "1" ? true : false);
					std::getline(CONFIG_FILE, STEAM_FOLDER);
					std::getline(CONFIG_FILE, DEMO_FOLDER);
				} else {
					CONFIG_FILE.close();
					DeleteFile(configFile.c_str());
				}
			} else {
				CONFIG_FILE.open(configFile.c_str(), std::ios::out);
			}


			if (hasConfig == false) {
				CONFIG_FILE << "CSGOVacCheckerConfigFile"; // header
				CONFIG_FILE << std::endl;

				char steamFolder[260];
				DWORD bufferSize = 260;

				if (RegGetValue(HKEY_CURRENT_USER, _T("Software\\Valve\\Steam\\"), _T("SteamPath"), RRF_RT_ANY,
					NULL, (PVOID)&steamFolder, &bufferSize)) {
					IS_STEAM_INSTALLLED = false;
					CONFIG_FILE << IS_STEAM_INSTALLLED;
					CONFIG_FILE << std::endl;
					CONFIG_FILE << "SEM STEAM INSTALADA";
					CONFIG_FILE << std::endl;
				} else {
					STEAM_FOLDER = std::string(steamFolder);
					IS_STEAM_INSTALLLED = true;
					CONFIG_FILE << IS_STEAM_INSTALLLED;
					CONFIG_FILE << std::endl;
					CONFIG_FILE << STEAM_FOLDER;
					CONFIG_FILE << std::endl;
				}
			}

			char defaultBrowser[260];
			DWORD browserSize = 260;

			RegGetValue(HKEY_CLASSES_ROOT, _T("http\\shell\\open\\command"), 0, RRF_RT_ANY,
				NULL, (PVOID)&defaultBrowser, &browserSize);

			DEFAULT_BROWSER = std::string(defaultBrowser);


			struct stat info;
			DEFAULT_CSGO_REPLAY_FOLDER = STEAM_FOLDER + "/steamapps/common/Counter-Strike Global Offensive/csgo/replays";

			if (stat(DEFAULT_CSGO_REPLAY_FOLDER.c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
				DEFAULT_CSGO_REPLAY_FOLDER_EXISTS = true;
			}

			int checkOption = 0;
			
			if (hasConfig == false && IS_STEAM_INSTALLLED && DEFAULT_CSGO_REPLAY_FOLDER_EXISTS) {
				checkOption = MessageBox(hwnd, "Procurar por demos na pasta padrão do CSGO?", "CSGOVacChecker - Pedrohenrique.ninja", MB_YESNO | MB_APPLMODAL);

				if (checkOption == IDYES) {
					DEMO_FOLDER = getWindowsLikePathString(DEFAULT_CSGO_REPLAY_FOLDER) + "\\";
				} else {
					DEMO_FOLDER = browseFolder(getWindowsLikePathString(DEFAULT_CSGO_REPLAY_FOLDER), "Selecione a pasta de demos");
					DEMO_FOLDER = DEMO_FOLDER + "\\";
				}

				CONFIG_FILE << DEMO_FOLDER;
				CONFIG_FILE << std::endl;
				CONFIG_FILE.flush();
			} else if(hasConfig == false)  {
				checkOption = MessageBox(hwnd, "A Steam ou CSGO não estão instalados, gostaria de informar a pasta de demos de outro lugar?", "CSGOVacChecker - Pedrohenrique.ninja", MB_YESNO);
				if (checkOption == IDYES) {
					DEMO_FOLDER = browseFolder("", "Selecione a pasta de demos");
					DEMO_FOLDER = DEMO_FOLDER + "\\";
					CONFIG_FILE << DEMO_FOLDER;
					CONFIG_FILE << std::endl;
					CONFIG_FILE.flush();
				} else {
					CONFIG_FILE.close();
					DeleteFile(configFile.c_str());
					CLOSE_APP = true;
					PostQuitMessage(0);
					return 0;
				}	
			}


			if (DEMO_FOLDER.empty() || DEMO_FOLDER == "/") {
				CONFIG_FILE.close();
				MessageBox(hwnd, "Você deve selecionar uma pasta!                             \nAdeus!", "CSGOVacChecker - Pedrohenrique.ninja", MB_OK);
				CLOSE_APP = true;
				DeleteFile(configFile.c_str());
				PostQuitMessage(0);
				return 0;
			}

			WIN32_FIND_DATA ffd;
			TCHAR szDir[MAX_PATH];
			LARGE_INTEGER filesize;
			HANDLE hFind = INVALID_HANDLE_VALUE;

			std::string demoFolderFilter = DEMO_FOLDER + "*.dem";
			StringCchCopy(szDir, MAX_PATH, demoFolderFilter.c_str());

			hFind = FindFirstFile(szDir, &ffd);

			if (INVALID_HANDLE_VALUE == hFind) {
				CONFIG_FILE.close();
				MessageBox(hwnd, "Erro ao conseguir dados da pasta, verifique se você selecionou uma pasta válida.\n", "CSGOVacChecker - Pedrohenrique.ninja", MB_OK);
				CLOSE_APP = true;
				DeleteFile(configFile.c_str());
				PostQuitMessage(0);
				return 0;
			}

			do {
				if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					// Dir
				} else {
					filesize.LowPart = ffd.nFileSizeLow;
					filesize.HighPart = ffd.nFileSizeHigh;
					DEMOS.push_back(std::string(ffd.cFileName));
				}
			} while (FindNextFile(hFind, &ffd) != 0);

			std::vector<std::string> parsedDemos;

			std::fstream demosParsed;
			demosParsed.open(demosParsedFile.c_str(), std::ios::in);
			
			if (demosParsed.is_open()) {
				std::string line;
				while (std::getline(demosParsed, line)) {
					parsedDemos.push_back(line);
				}
				demosParsed.close();
			}
			
			// 'remove if contains
			DEMOS.erase(std::remove_if(DEMOS.begin(), DEMOS.end(),[&parsedDemos](const std::string& s) {
				auto it = find(parsedDemos.begin(), parsedDemos.end(), s);
				return (it != parsedDemos.end());
			}), DEMOS.end());


			if (DEMOS.empty() && parsedDemos.size() <= 0) {
				CONFIG_FILE.close();
				MessageBox(hwnd, "Nenhuma demo encontrada na pasta selecionada, verifique se você selecionou a pasta correta.", "CSGOVacChecker - Pedrohenrique.ninja", MB_OK);
				CLOSE_APP = true;
				PostQuitMessage(0);
				return 0;
			}

			PROGRESS_BAR = CreateWindowEx(0, PROGRESS_CLASS,
				"CSGOVacChecker", WS_CHILD | WS_VISIBLE,
				CW_USEDEFAULT, CW_USEDEFAULT,
				500, 60, hwnd, (HMENU)0,
				(HINSTANCE)GetWindowLong(hwnd, GWL_HINSTANCE), NULL);

			ShowWindow(hwnd, SW_SHOW);
			SendMessage(PROGRESS_BAR, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

			PROCESS_ACTION = PROCESS;

			
		} break;
		default: {
			return DefWindowProc(hwnd, msg, wParam, lParam);
		} break;
	}

	return 0;
}

void workerThread() {
	while (CLOSE_APP == false) {
		if (PROCESS_ACTION == PROCESS) {
			
			// get the players
			std::fstream playersParsedGet;
			playersParsedGet.open(playersParsedFile.c_str(), std::ios::in | std::ios::binary);

			if (playersParsedGet.is_open()) {
				uint32_t numPlayers;

				playersParsedGet.read((char*)&numPlayers, sizeof(uint32_t));
	
				for (uint32_t n = 0; n < numPlayers; n++) {
					uint64_t xuid;
					playersParsedGet.read((char*)&xuid, sizeof(uint64_t));
					PlayerInfoSimple player;
					playersParsedGet.read((char*)&player, sizeof(PlayerInfoSimple));

					if (xuid != 0) {
						PLAYERS_ID[xuid] = player;
					}
				}
				playersParsedGet.close();
			}


			if (DEMOS.size() > 0) {
				PROGRESS_STEP = 100.0f / DEMOS.size();
				PROGRESS = 0.0f;
				TOTAL_DEMOS = DEMOS.size();

				for (int i = 0; i < TOTAL_DEMOS && CLOSE_APP == false; i++) {
					std::string currentDemo = DEMO_FOLDER + DEMOS[i];

					std::stringstream title;
					title << "CSGOVacChecker Analisando - ";
					title << i + 1;
					title << "/";
					title << TOTAL_DEMOS;

					PROGRESS = PROGRESS + PROGRESS_STEP;
					SetWindowText(MAIN_WINDOW, title.str().c_str());
					SendMessage(PROGRESS_BAR, PBM_SETPOS, (WPARAM)((int)PROGRESS), 0);

					CSGOSimpleDemo demo(currentDemo);
					demo.parseAll();

					auto players = demo.getPlayers();

					for (auto it = players.begin(); it != players.end() && CLOSE_APP == false; it++) {
						if (it->xuid != 0) {
							PlayerInfoSimple info;
							std::memcpy(info.demoFile, DEMOS[i].c_str(), DEMOS[i].length());
							std::memcpy(info.name, it->name, MAX_PLAYER_NAME_LENGTH);
							PLAYERS_ID[it->xuid] = info;
						}
					}
				}
			

				// save the players
				std::fstream playersParsed;
				playersParsed.open(playersParsedFile.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);

				if (playersParsed.is_open()) {
					uint32_t numPlayers = PLAYERS_ID.size();
					playersParsed.write((char*)&numPlayers, sizeof(uint32_t));
					for (auto pit = PLAYERS_ID.begin(); pit != PLAYERS_ID.end(); pit++) {
						playersParsed.write((char*)&pit->first, sizeof(uint64_t));
						playersParsed.write((char*)&pit->second, sizeof(PlayerInfoSimple));
					}
					playersParsed.close();
				}

			

				// save the demos parsed
				std::fstream demosParsed;
				demosParsed.open(demosParsedFile.c_str(), std::ios::out|std::ios::app);

				if (demosParsed.is_open()) {
					for (int d = 0; d < DEMOS.size(); d++) {
						demosParsed << DEMOS[d];
						demosParsed << std::endl;
					}
					demosParsed.close();
				}
			} // if DEMOS > 0

			if (PLAYERS_ID.size() <= 0) {
				MessageBox(MAIN_WINDOW, "Nenhum player para ser verificado", "CSGOVacChecker - Pedrohenrique.ninja", MB_OK);
				CLOSE_APP = true;
				PostMessage(MAIN_WINDOW, WM_QUIT, 0, 0);
				return;
			}

			// reset progress
			SendMessage(PROGRESS_BAR, PBM_SETPOS, (WPARAM)((int)0), 0);

			PROGRESS_STEP = 100.0f / PLAYERS_ID.size();
			PROGRESS = 0.0f;
			int totalPlayers = PLAYERS_ID.size();
			int _currentPlayer = 0;

			// Get network info
			std::string networkURL = STEAM_API_CALL;

			for (auto it = PLAYERS_ID.begin(); it != PLAYERS_ID.end() && CLOSE_APP == false; it++) {
				std::stringstream ss;
				ss << networkURL;
				ss << it->first;
				ss << ",";
				networkURL = ss.str();

				if (networkURL.length() > 1980) { // just to avoid any problem with long http request
					LIST_GET_REQUESTS.push_back(networkURL);
					networkURL = STEAM_API_CALL;
				}

				std::stringstream title;
				title << "CSGOVacChecker Verificando contas - ";
				title << _currentPlayer + 1;
				title << "/";
				title << totalPlayers;

				PROGRESS = PROGRESS + PROGRESS_STEP;
				SetWindowText(MAIN_WINDOW, title.str().c_str());
				SendMessage(PROGRESS_BAR, PBM_SETPOS, (WPARAM)((int)PROGRESS), 0);
				_currentPlayer++;
			}

			// the last element
			LIST_GET_REQUESTS.push_back(networkURL);


			PROGRESS_STEP = 100.0f / LIST_GET_REQUESTS.size();
			PROGRESS = 0.0f;
			int totalRequests = LIST_GET_REQUESTS.size();
			int _currentRequest = 0;

			for (size_t i = 0; i < LIST_GET_REQUESTS.size() && CLOSE_APP == false; i++) {
				std::string downloadLink = LIST_GET_REQUESTS[i];
				std::string downloadFileName = DEFAULT_DOWNLOAD_LOCATION + "csgo_check_" + std::to_string(i) + ".json";
				URLDownloadToFile(NULL, downloadLink.c_str(), downloadFileName.c_str(), 0, NULL);


				std::stringstream title;
				title << "CSGOVacChecker Baixando dados da Valve - ";
				title << _currentRequest + 1;
				title << "/";
				title << totalRequests;

				PROGRESS = PROGRESS + PROGRESS_STEP;
				SetWindowText(MAIN_WINDOW, title.str().c_str());
				SendMessage(PROGRESS_BAR, PBM_SETPOS, (WPARAM)((int)PROGRESS), 0);
				_currentRequest++;
			}

			// reset progress
			SendMessage(PROGRESS_BAR, PBM_SETPOS, (WPARAM)((int)PROGRESS), 0);




			// try to parse the data
			for (size_t i = 0; i < LIST_GET_REQUESTS.size() && CLOSE_APP == false; i++) {
				std::string file = DEFAULT_DOWNLOAD_LOCATION + "csgo_check_" + std::to_string(i) + ".json";
				
				std::ifstream dataStream(file);
				
				
				if (!dataStream.is_open()) {
					MessageBox(MAIN_WINDOW, "Erro ao abrir um arquivo de verificação de bans, provavelmente\n ocorreu algum erro na hora de efetuar o download do mesmo.", "CSGOVacChecker - Pedrohenrique.ninja", MB_OK);
					CLOSE_APP = true;
					PostMessage(MAIN_WINDOW, WM_QUIT, 0, 0);
					return;
				} else {
					json jsonData(dataStream);
					dataStream.close();


					PROGRESS_STEP = 100.0f / jsonData["players"].size();
					PROGRESS = 0.0f;
					int totalPlayers = jsonData["players"].size();
					int _currentPlayer = 0;

					for (json::iterator it = jsonData["players"].begin(); it != jsonData["players"].end() && CLOSE_APP == false; ++it) {
						int numGameBans = (*it)["NumberOfGameBans"];

						if ((*it)["VACBanned"] || (*it)["CommunityBanned"] || numGameBans > 0) {
							BANINFO ban;
							std::string steamID = (*it)["SteamId"];
							ban.steamID = steamID;
							ban.communityBanned = (*it)["CommunityBanned"];
							ban.VACBanned = (*it)["VACBanned"];
							ban.numberOfVACBans = (*it)["NumberOfVACBans"];
							ban.daysSinceLastBan = (*it)["DaysSinceLastBan"];
							ban.numberOfGameBans = numGameBans;
							std::string economyBan = (*it)["EconomyBan"];
							ban.economyBan = economyBan;
							
							PLAYERS_BANNED.push_back(ban);
						}


						std::stringstream title;
						title << "CSGOVacChecker Verificando meliantes - ";
						title << _currentPlayer + 1;
						title << "/";
						title << totalPlayers;

						PROGRESS = PROGRESS + PROGRESS_STEP;
						SetWindowText(MAIN_WINDOW, title.str().c_str());
						SendMessage(PROGRESS_BAR, PBM_SETPOS, (WPARAM)((int)PROGRESS), 0);
						_currentPlayer++;
					}
					SendMessage(PROGRESS_BAR, PBM_SETPOS, (WPARAM)((int)0), 0);
					
					// delete json file
					DeleteFile(getWindowsLikePathString(file).c_str());
				}
				
			}


			if (PLAYERS_BANNED.size() > 0) {
				SetWindowText(MAIN_WINDOW, "CSGOVacChecker - Gerando resultados");

				std::string output;

				std::string HTML_HEADER;
				std::string HTML_FOOTER;

				DWORD sizeRC = 0;
				const char* dataRC = NULL;
				LoadFileInResource(IDI_HEADER, HTMLFILE, sizeRC, dataRC);
				HTML_HEADER = std::string(dataRC);

				LoadFileInResource(IDI_FOOTER, HTMLFILE, sizeRC, dataRC);
				HTML_FOOTER = std::string(dataRC);

				output += HTML_HEADER;

				PROGRESS_STEP = 100.0f / PLAYERS_BANNED.size();
				PROGRESS = 0.0f;
				int totalPlayersBanned = PLAYERS_BANNED.size();
				int _currentPlayerBanned = 0;

				// sort for day of last ban
				std::sort(PLAYERS_BANNED.begin(), PLAYERS_BANNED.end(), compareByLastBan);

				for (size_t i = 0; i < PLAYERS_BANNED.size() && CLOSE_APP == false; i++) {
					std::string communityBanned = PLAYERS_BANNED[i].communityBanned ? "<span class='ban_yes'>Sim</span>" : "N&atilde;o";
					std::string VACBanned = PLAYERS_BANNED[i].VACBanned ? "<span class='ban_yes'>Sim</span>" : "N&atilde;o";
					std::string numVacBan = std::to_string(PLAYERS_BANNED[i].numberOfVACBans);
					std::string daysSinceLastBan = std::to_string(PLAYERS_BANNED[i].daysSinceLastBan);
					std::string numGameBans = std::to_string(PLAYERS_BANNED[i].numberOfGameBans);

					PlayerInfoSimple info = PLAYERS_ID[strtoll(PLAYERS_BANNED[i].steamID.c_str(), 0, 10)];
					std::string demo = info.demoFile;

					std::string row;
					row += "<tr><td><a href='http://steamcommunity.com/profiles/" + PLAYERS_BANNED[i].steamID + "' target='_blank'>" + info.name + "</a></td>";
					row += "<td>" + communityBanned + "</td>";
					row += "<td>" + VACBanned + "</td>";
					row += "<td>" + numVacBan + "</td>";
					row += "<td>" + daysSinceLastBan + "</td>";
					row += "<td>" + numGameBans + "</td>";
					row += "<td>" + PLAYERS_BANNED[i].economyBan + "</td>";


					if (IS_STEAM_INSTALLLED) {
						std::string absDemo = DEMO_FOLDER + demo;
						std::string openDemoLink = PLAY_DEMO + convertSpaceToP20(absDemo);
						std::string openDemoLinkHighLights = openDemoLink + "%20" + PLAYERS_BANNED[i].steamID;
						row += "<td><a href='" + openDemoLink + "'>Ver partida completa</a><br><a href='" + openDemoLinkHighLights + "'>Ver Highlights</a></td></tr>";
					} else {
						row += "<td>" + demo + "</td></tr>";
					}

					output += row;

					std::stringstream title;
					title << "CSGOVacChecker Adicionando meliantes a lista - ";
					title << _currentPlayerBanned + 1;
					title << "/";
					title << totalPlayersBanned;

					PROGRESS = PROGRESS + PROGRESS_STEP;
					SetWindowText(MAIN_WINDOW, title.str().c_str());
					SendMessage(PROGRESS_BAR, PBM_SETPOS, (WPARAM)((int)PROGRESS), 0);
					_currentPlayerBanned++;
				}


				time_t rawtime;
				struct tm * timeinfo;
				char buffer[80];

				time(&rawtime);
				timeinfo = localtime(&rawtime);

				strftime(buffer, 80, "%d/%m/%Y %I:%M:%S", timeinfo);
				std::string dataInfo(buffer);


				output += "</tbody></table><div id ='stats'>" + std::to_string(PLAYERS_BANNED.size()) + " bans de " + std::to_string(PLAYERS_ID.size()) + " Player's - Resultado gerado em " + dataInfo + "</div>";
				output += HTML_FOOTER;

				ShowWindow(MAIN_WINDOW, SW_HIDE);

				std::fstream resultOutput;
				std::string  resultOutputFile = DEFAULT_DOWNLOAD_LOCATION + "resultados.html";

				resultOutput.open(resultOutputFile.c_str(), std::ios::out);

				if (resultOutput.is_open()) {
					resultOutput.write(output.c_str(), output.size());
					resultOutput.close();

					std::string executeCmd = DEFAULT_BROWSER;

					size_t posExeCmd = executeCmd.find_last_of("\\\\");
					
					if (posExeCmd != std::string::npos) {
						executeCmd = executeCmd.substr(posExeCmd + 1);

						posExeCmd = executeCmd.find_last_of(".exe");
						if (posExeCmd != std::string::npos) {
							executeCmd = executeCmd.substr(0, posExeCmd + 1);
						}
					} else {
						executeCmd = resultOutputFile;
						resultOutputFile = "";
					}	

					ShellExecute(0, "open", executeCmd.c_str(), resultOutputFile.c_str(), 0, SW_SHOWDEFAULT);

					CLOSE_APP = true;
					PostMessage(MAIN_WINDOW, WM_QUIT, 0, 0);
					return;
				} else {
					MessageBox(MAIN_WINDOW, "Não foi possível salvar o arquivo com os resultados.", "CSGOVacChecker - Pedrohenrique.ninja", MB_OK);
					CLOSE_APP = true;
					PostMessage(MAIN_WINDOW, WM_QUIT, 0, 0);
					return;
				}

			} else {
				ShowWindow(MAIN_WINDOW, SW_HIDE);
				MessageBox(MAIN_WINDOW, "Easy Peasy Lemon Squeezy, você não jogou contra nenhum Cheater!\n\n Ou a Valve não deu ban neles ainda.", "CSGOVacChecker - Pedrohenrique.ninja", MB_OK);
				CLOSE_APP = true;
				PostMessage(MAIN_WINDOW, WM_QUIT, 0, 0);
				return;
			}


			// close the app
			CLOSE_APP = true;
			PROCESS_ACTION = WAIT;
			PostMessage(MAIN_WINDOW, WM_QUIT, 0, 0);
			return;
		} 
	}
}

std::string getWindowsLikePathString(std::string path) {
	size_t pos;
	while ((pos = path.find_first_of("/")) != std::string::npos) {
		path.replace(pos, 1, "\\");
	}

	return path;
}

int CALLBACK browseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData) {

	if (uMsg == BFFM_INITIALIZED) {
		std::string tmp = (const char *)lpData;
		SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
	}

	return 0;
}

std::string browseFolder(std::string saved_path, std::string title) {
	TCHAR path[MAX_PATH];

	const char * path_param = saved_path.c_str();

	BROWSEINFO bi = { 0 };
	bi.lpszTitle = title.c_str();
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
	bi.lpfn = browseCallbackProc;
	bi.lParam = (LPARAM)path_param;

	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);

	if (pidl != 0) {
		//get the name of the folder and put it in path
		SHGetPathFromIDList(pidl, path);

		//free memory used
		IMalloc * imalloc = 0;
		if (SUCCEEDED(SHGetMalloc(&imalloc))) {
			imalloc->Free(pidl);
			imalloc->Release();
		}

		return path;
	}

	return "";
}

void LoadFileInResource(int name, int type, DWORD& size, const char*& data) {
	HMODULE handle = ::GetModuleHandle(NULL);
	HRSRC rc = ::FindResource(handle, MAKEINTRESOURCE(name),
		MAKEINTRESOURCE(type));
	HGLOBAL rcData = ::LoadResource(handle, rc);
	size = ::SizeofResource(handle, rc);
	data = static_cast<const char*>(::LockResource(rcData));
}


std::string convertSpaceToP20(const std::string &string) {
	std::string result = string;

	size_t p;
	while ((p = result.find_first_of(" ")) != std::string::npos) {
		result.replace(p, 1, "%20");
	}

	return result;
}

bool compareByLastBan(const BANINFO &a, const BANINFO &b) {
	return a.daysSinceLastBan < b.daysSinceLastBan;
}