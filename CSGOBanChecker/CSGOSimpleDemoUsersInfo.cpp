#include "CSGOSimpleDemoUsersInfo.h"

CSGOSimpleDemo::CSGOSimpleDemo() : m_demoOpen(false), m_inGameTick(0), m_currentTick(0), m_demoParsed(false) {
}

CSGOSimpleDemo::CSGOSimpleDemo(const std::string &fileName) : m_demoOpen(false), m_inGameTick(0), m_currentTick(0),
m_demoParsed(false) {
	open(fileName);
}

bool CSGOSimpleDemo::open(const std::string &fileName) {
	std::string buffer;

	m_fileName.clear();
	m_fileName = fileName;

	if (m_buffer) {
		delete m_buffer;
		m_buffer = 0;
	}

	if (m_stream) {
		delete m_stream;
		m_stream = 0;
	}

	std::ifstream file;
	file.open(fileName.c_str(), std::ios::in | std::ios::binary);

	if (!file.is_open()) {
		std::cerr << "CSGOSimpleDemo::open: Could not open the demo file " << fileName << std::endl;
		return false;
	}

	size_t fileSize;

	file.seekg(0, std::ios_base::end);
	fileSize = static_cast<size_t>(file.tellg());
	file.seekg(0, std::ios_base::beg);

	if (fileSize < sizeof(m_header)) {
		std::cerr << "CSGOSimpleDemo::open: File " << fileName << " too small " << fileSize << std::endl;
		file.close();
		return false;
	}

	buffer.resize(fileSize);
	file.read((char*)&buffer[0], fileSize);
	file.close();

	m_buffer = new DataBuffer(buffer.c_str(), fileSize);
	m_stream = new DataStream(*m_buffer);

	m_header.parseFromMemory(*m_stream);

	if (std::string(m_header.demoFileStamp) != DEMO_HEADER_ID) {
		std::cerr << "CSGOSimpleDemo::open: File " << fileName << " has an invalid demo header ID: "
			<< m_header.demoFileStamp << " expected: "
			<< DEMO_HEADER_ID << std::endl;
		return false;
	}

	if (m_header.demoProtocol != DEMO_PROTOCOL) {
		std::cerr << "CSGOSimpleDemo::open: File " << fileName << " has an invalid demo protocol: "
			<< m_header.demoProtocol << " expected: "
			<< DEMO_PROTOCOL << std::endl;
		return false;
	}

	m_demoOpen = true;

	return true;
}

void CSGOSimpleDemo::parseAll() {
	if (!m_demoOpen) {
		std::cerr << "CSGOSimpleDemo::parseAll: Demo not open" << std::endl;
		return;
	}

	while (parseNextTick()) {}

	int x;
	x = 0;
}

bool CSGOSimpleDemo::parseNextTick() {
	if (!m_demoOpen) {
		std::cerr << "CSGOSimpleDemo::parseNextTick: Demo not open" << std::endl;
		return false;
	}

	bool canParseTick = parseTick();

	if (!canParseTick) {
		if (m_inGameTick >= m_header.playblackTicks) {
			m_demoParsed = true;
		}
	}

	return canParseTick;
}

bool CSGOSimpleDemo::parseTick() {
	DemoCommand command = static_cast<DemoCommand>(m_stream->readByte());

	if (command < DemoCommand::SIGNON) {
		std::cerr << "CSGOSimpleDemo::parseTick: Missing end tag in demo file." << std::endl;
		command = DemoCommand::STOP;
		return false;
	}

	assert(command >= DemoCommand::SIGNON && command <= DemoCommand::LASTCMD);

	m_inGameTick = m_stream->readInt32();
	char playerSlot = m_stream->readByte(); // read player slot

	m_currentTick++;

	switch (command) {
		case DemoCommand::SYNCTICK:
		{} break;
		case DemoCommand::STOP:
		{
			std::cout << "STOP Command" << std::endl;
			return false;
		} break;
		case DemoCommand::CONSOLECMD:
		{
			std::cout << "CONSOLECMD Command" << std::endl;
			readRawData(0, 0);
		} break;
		case DemoCommand::DATATABLES:
		{
			parseDataTables();
		} break;
		case DemoCommand::STRINGTABLES:
		{
			parseStringTables();
		} break;
		case DemoCommand::USERCMD:
		{
			int outgoingSequence = m_stream->readInt32();
			readRawData(0, 0);
		} break;
		case DemoCommand::SIGNON:
		case DemoCommand::PACKET:
		{
			parsePacket();
		} break;
		default:
		{
			std::cerr << "CSGOSimpleDemo::parseTick: Can't handle command: " << static_cast<int>(command) << std::endl;
			return true;
		} break;
	}

	return true;
}

bool CSGOSimpleDemo::parseDataTables() {
	int dataSize = m_stream->readInt32();

	assert(dataSize > 0);

	char *charBuffer = new char[dataSize];
	m_stream->readBytesToBuffer(charBuffer, dataSize);

	DataBuffer buffer(charBuffer, dataSize);
	DataStream stream(buffer);
	delete[] charBuffer;

	while (1) {
		CSVCMsg_SendTable msg;
		stream.readVarInt32(); // read the type
		int32 size = stream.readVarInt32();

		if (size < 0 || size > NET_MAX_PAYLOAD) {
			std::cerr << "CSGOSimpleDemo::parseDataTables: size < 0 or > NET_MAX_PAYLOAD (" << size << ")" << std::endl;
			return false;
		}

		if (size > DEMO_RECORD_BUFFER_SIZE) {
			std::cerr << "CSGOSimpleDemo::parseDataTables: size > DEMO_RECORD_BUFFER_SIZE (" << size << ")" << std::endl;
			return false;
		}

		char *msgBuff = new char[size];
		stream.readBytesToBuffer(msgBuff, size);
		msg.ParseFromArray(msgBuff, size);
		delete[] msgBuff;


		if (msg.is_end()) {
			break;
		}

		m_dataTables.push_back(msg);
	}
	

	uint16 numServerClass = stream.readInt16();

	for (uint32 i = 0; i < numServerClass; i++) {
		ServerClass entry;
		entry.nClassID = stream.readInt16();

		if (entry.nClassID >= numServerClass) {
			std::cerr << "CSGOSimpleDemo::parseDataTables: entry.nClassID(" << entry.nClassID << ") >= numServerClass" << std::endl;
			return false;
		}

		entry.strName = stream.readString(256, false);
		entry.strDTName = stream.readString(256, false);
		entry.nDataTable = -1;

		for (uint32 dataTableIndex = 0; dataTableIndex < m_dataTables.size(); dataTableIndex++) {
			if (m_dataTables[dataTableIndex].net_table_name() == entry.strDTName) {
				entry.nDataTable = dataTableIndex;
				break;
			}
		}

		m_serverClasses.push_back(entry);
	}
	

	for (uint32 i = 0; i < m_serverClasses.size(); i++) {
		flattenDataTable(i);
	}
	

	// perform integer log2() to set s_nServerClassBits
	int nTemp = numServerClass;
	m_serverClassBits = 0;
	while (nTemp >>= 1) ++m_serverClassBits;

	m_serverClassBits++;

	return true;
}

void CSGOSimpleDemo::parseUserInfo(DataBitStream &data, const uint32 &entityID) {
	uint32 playerID = entityID + 1; // We have to add 1 to the player entity to get the right data
	
	PlayerInfo playerInfo;
	playerInfo.parseFromMemory(data);

	m_players.push_back(playerInfo);
}

void CSGOSimpleDemo::parseStringTables() {
	int dataSize = m_stream->readInt32();

	assert(dataSize > 0);

	char *charBuffer = new char[dataSize];
	m_stream->readBytesToBuffer(charBuffer, dataSize);

	DataBitStream stream(charBuffer, dataSize);
	delete[] charBuffer;

	int numTables = stream.readByte();

	for (int i = 0; i < numTables; i++) {
		std::string tableName = stream.readString(256);

		if (tableName.length() > 100) {
			std::cerr << "CSGOSimpleDemo::parseStringTable: Table name > 100" << std::endl;
			return;
		}

		parseStringTable(tableName, stream);
	}
}

void CSGOSimpleDemo::parseStringTable(const std::string &tableName, DataBitStream &stream) {
	uint16 numStrings = stream.readInt16();

	//std::cout << "TableName: " << tableName << " numstrings: " << numStrings << std::endl;

	for (uint16 i = 0; i < numStrings; i++) {
		std::string stringName = stream.readString(256);

		if (stringName.length() > 100) {
			std::cerr << "CSGOSimpleDemo::parseStringTable: String name > 100" << std::endl;
			return;
		}

		bool hasData = stream.readBit();

		if (hasData) {
			uint16 dataSize = stream.readInt16();
			
			char *data = new char[dataSize + 4];
			stream.readBytesToBuffer(data, dataSize);
			
			//std::cout << tableName << std::endl;
			if (tableName == "userinfo") {
				DataBitStream dataBitStream(data, dataSize);
				parseUserInfo(dataBitStream, i);
			}

			delete[] data;

		}
	}

	bool hasClientData = stream.readBit();

	if (hasClientData) {
		std::cout << "Wow, this demo has client data and I dont have a clue what that means" << std::endl;
		uint16 numStrings = stream.readInt16();

		for (uint16 i = 0; i < numStrings; i++) {
			std::string stringName = stream.readString(256);

			if (stringName.length() > 100) {
				std::cerr << "CSGOSimpleDemo::parseStringTable: String name (client data) > 100" << std::endl;
				return;
			}

			bool hasData = stream.readBit();

			if (hasData) {
				uint16 dataSize = stream.readInt16();

				char *data = new char[dataSize + 4];
				stream.readBytesToBuffer(data, dataSize);
				// handle data?
				delete[] data;
			}
		}
	}

}

void CSGOSimpleDemo::readRawData(char *buffer, int bufferSize) {
	int32 readSize = m_stream->readInt32();

	if (buffer && (bufferSize < readSize)) {
		std::cerr << "CSGOSimpleDemo::readRawData: Buffer overflow BufferSize:"
			<< bufferSize << " trying to write: " << readSize << std::endl;
		return;
	}

	if (buffer) {
		m_stream->readBytesToBuffer(buffer, readSize);
	} else {
		char *tmpBuff = new char[readSize];
		m_stream->readBytesToBuffer(tmpBuff, readSize);
		delete[] tmpBuff;
	}
}

void CSGOSimpleDemo::parseSetConVar(CNETMsg_SetConVar &setConVar) {
	uint32 numConVars = setConVar.convars().cvars_size();

	for (uint32 i = 0; i < numConVars; i++) {
		CMsg_CVars_CVar conVar = setConVar.convars().cvars(i);
		m_conVars[conVar.name()] = conVar.value();
	}
}

void CSGOSimpleDemo::parseServerInfo(CSVCMsg_ServerInfo &serverInfo) {
	// convert a CSVCMsg_ServerInfo to a ServerInfo
	// easy to handle
	m_serverInfo.protocol = serverInfo.protocol();
	m_serverInfo.serverCount = serverInfo.server_count();
	m_serverInfo.isDedicated = serverInfo.is_dedicated();
	m_serverInfo.isOfficialValveServer = serverInfo.is_official_valve_server();
	m_serverInfo.isHLTV = serverInfo.is_hltv();
	m_serverInfo.isReplay = serverInfo.is_replay();
	m_serverInfo.isRedirectingToProxyRelay = serverInfo.is_redirecting_to_proxy_relay();
	m_serverInfo.cOS = serverInfo.c_os();
	m_serverInfo.mapCRC = serverInfo.map_crc();
	m_serverInfo.clientCRC = serverInfo.client_crc();
	m_serverInfo.stringTableCRC = serverInfo.string_table_crc();
	m_serverInfo.maxClients = serverInfo.max_clients();
	m_serverInfo.maxClasses = serverInfo.max_classes();
	m_serverInfo.playerSlot = serverInfo.player_slot();
	m_serverInfo.tickInterval = serverInfo.tick_interval();
	m_serverInfo.gameDir = serverInfo.game_dir();
	m_serverInfo.mapName = serverInfo.map_name();
	m_serverInfo.mapGroupName = serverInfo.map_group_name();
	m_serverInfo.skyName = serverInfo.sky_name();
	m_serverInfo.hostName = serverInfo.host_name();
	m_serverInfo.ugcMapID = serverInfo.ugc_map_id();
}

void CSGOSimpleDemo::parseNetTick(CNETMsg_Tick &netTick) {
	// netTick contains only
	// tick: Current Tick
	// host_computationtime: How much time the server take to process one tick result in microseconds(u) (4ms for 64tick)
	// host_computationtime_std_deviation: standard deviation or variation in microseconds(u)
	// host_framestarttime_std_deviation: time to the server wake up from a sleep call after meeting the server framerate
	NetTick tick;
	tick.tick = netTick.tick();
	tick.hostComputationTime = netTick.host_computationtime();
	tick.hostComputationTimeStdDeviation = netTick.host_computationtime_std_deviation();
	tick.hostFrameStartTimeStdDeviation = netTick.host_framestarttime_std_deviation();

	m_netTicks.push_back(tick);
}

void CSGOSimpleDemo::parsePacket() {
	m_commandInfo.parseFromMemory(*m_stream);
	SequenceInfo sequenceInfo;
	sequenceInfo.parseFromMemory(*m_stream);

	int dataLength = m_stream->readInt32();
	std::streampos destination = m_stream->tellg() + static_cast<std::streampos>(dataLength);

	while (m_stream->tellg() < destination) {
		int32 command = m_stream->readVarInt32();
		int32 size = m_stream->readVarInt32();
		char *data = new char[size];
		m_stream->readBytesToBuffer(data, size);
		// for getting the users we dont need any of this info
		delete[] data;
	}
}

bool CSGOSimpleDemo::isPropExcluded(CSVCMsg_SendTable &table, const CSVCMsg_SendTable::sendprop_t &checkSendProp) {
	for (uint32 i = 0; i < m_excludedEntries.size(); i++) {
		if (table.net_table_name() == m_excludedEntries[i].DTName &&
			checkSendProp.var_name() == m_excludedEntries[i].varName) {
			return true;
		}
	}
	return false;
}

void CSGOSimpleDemo::flattenDataTable(int32 nServerClass) {
	CSVCMsg_SendTable *table = &m_dataTables[m_serverClasses[nServerClass].nDataTable];
	m_excludedEntries.clear();

	gatherExcludes(table);
	gatherProps(table, nServerClass);

	std::vector<FlattenedPropEntry> &flattenedProps = m_serverClasses[nServerClass].flattenedProps;
	std::vector<uint32> priorities;
	priorities.push_back(64);

	for (uint32 i = 0; i < flattenedProps.size(); i++) {
		uint32 priority = flattenedProps[i].m_prop->priority();

		bool found = false;

		for (uint32 j = 0; j < priorities.size(); j++) {
			if (priorities[j] == priority) {
				found = true;
				break;
			}
		}

		if (!found) {
			priorities.push_back(priority);
		}
	}

	std::sort(priorities.begin(), priorities.end());

	// sort flattenedProps by priority
	uint32 start = 0;
	for (uint32 priorityIndex = 0; priorityIndex < priorities.size(); priorityIndex++) {
		uint32 priority = priorities[priorityIndex];

		while (true) {
			uint32 currentProp = start;
			while (currentProp < flattenedProps.size()) {
				const FlattenedPropEntry &key = flattenedProps[currentProp];

				if (key.m_prop->priority() == priority || (priority == 64 && (SPROP_CHANGES_OFTEN & key.m_prop->flags()))) {
					if (start != currentProp) {
						FlattenedPropEntry temp = flattenedProps[start];
						flattenedProps[start] = flattenedProps[currentProp];
						flattenedProps[currentProp] = temp;
					}
					start++;
					break;
				}
				currentProp++;
			}

			if (currentProp == flattenedProps.size()) {
				break;
			}
		}
	}
}

void CSGOSimpleDemo::gatherProps(CSVCMsg_SendTable *table, int32 nServerClass) {
	std::vector<FlattenedPropEntry> tempFlattenedProps;
	gatherPropsIterateProps(table, nServerClass, &tempFlattenedProps);

	std::vector<FlattenedPropEntry> &flattenedProps = m_serverClasses[nServerClass].flattenedProps;
	for (uint32 i = 0; i < tempFlattenedProps.size(); i++) {
		flattenedProps.push_back(tempFlattenedProps[i]);
	}
}

void CSGOSimpleDemo::gatherPropsIterateProps(CSVCMsg_SendTable *table, int32 nServerClass, std::vector<FlattenedPropEntry> *flattenedProps) {
	for (int32 iProp = 0; iProp < table->props_size(); iProp++) {
		const CSVCMsg_SendTable::sendprop_t& sendProp = table->props(iProp);

		if ((sendProp.flags() & SPROP_INSIDEARRAY) || (sendProp.flags() & SPROP_EXCLUDE) || isPropExcluded(*table, sendProp)) {
			continue;
		}

		if (static_cast<SendPropType>(sendProp.type()) == SendPropType::DPT_DataTable) {
			CSVCMsg_SendTable *subTable = getTableByName(sendProp.dt_name());
			if (subTable) {
				if (sendProp.flags() & SPROP_COLLAPSIBLE) {
					gatherPropsIterateProps(subTable, nServerClass, flattenedProps);
				} else {
					gatherProps(subTable, nServerClass);
				}
			}
		} else {
			if (static_cast<SendPropType>(sendProp.type()) == SendPropType::DPT_Array) {
				flattenedProps->push_back(FlattenedPropEntry(&sendProp, &(table->props(iProp - 1))));
			} else {
				flattenedProps->push_back(FlattenedPropEntry(&sendProp, 0));
			}
		}
	}
}

void CSGOSimpleDemo::gatherExcludes(CSVCMsg_SendTable *table) {

	for (int32 iProp = 0; iProp < table->props_size(); iProp++) {
		const CSVCMsg_SendTable::sendprop_t& sendProp = table->props(iProp);

		if (sendProp.flags() & SPROP_EXCLUDE) {
			ExcludeEntry entry;
			entry.varName = sendProp.var_name();
			entry.DTName = sendProp.dt_name();
			entry.DTExcluding = table->net_table_name();
			m_excludedEntries.push_back(entry);
		}

		if (static_cast<SendPropType>(sendProp.type()) == SendPropType::DPT_DataTable) {
			CSVCMsg_SendTable *subTable = getTableByName(sendProp.dt_name());
			if (subTable) {
				gatherExcludes(subTable);
			}
		}
	}

}

CSVCMsg_SendTable* CSGOSimpleDemo::getTableByName(const std::string name) {
	for (uint32 i = 0; i < m_dataTables.size(); i++) {
		if (m_dataTables[i].net_table_name() == name) {
			return &(m_dataTables[i]);
		}
	}
	return 0;
}

CSGOSimpleDemo::~CSGOSimpleDemo() {
	delete m_buffer;
	delete m_stream;
}

std::ostream& operator<<(std::ostream &out, const DemoHeader &header) {
	out << "------------HEADER------------" << std::endl;
	out << "demofilestamp: " << header.demoFileStamp << std::endl;
	out << "demoprotocol: " << header.demoProtocol << std::endl;
	out << "networkprotocol: " << header.networkProtocol << std::endl;
	out << "servername: " << header.serverName << std::endl;
	out << "clientname: " << header.clientName << std::endl;
	out << "mapname: " << header.mapName << std::endl;
	out << "gamedir: " << header.gameDir << std::endl;
	out << "playblack_time: " << header.playblackTime << std::endl;
	out << "playblack_ticks: " << header.playblackTicks << std::endl;
	out << "playblack_frames: " << header.playblackFrames << std::endl;
	out << "signonlength: " << header.signonLength << std::endl;
	out << "server tick rate: " << header.getTickRate() << std::endl;
	out << "------------HEADER------------" << std::endl;
	return out;
}

std::ostream& operator<<(std::ostream &out, const PlayerInfo &player) {
	out << "------------PLAYER------------" << std::endl;
	out << "version: " << player.version << std::endl;
	out << "xuid: " << player.xuid << std::endl;
	out << "name: " << player.name << std::endl;
	out << "userID: " << player.userID << std::endl;
	out << "guid: " << player.guid << std::endl;
	out << "friendsID: " << player.friendsID << std::endl;
	out << "friendsName: " << player.friendsName << std::endl;
	out << "isFakePlayer: " << ((player.isFakePlayer) ? "true" : "false") << std::endl;
	out << "isHLTVProxy: " << ((player.isHLTVProxy) ? "true" : "false") << std::endl;

	out << "customFiles: ";
	for (uint32 i = 0; i < MAX_CUSTOM_FILES; i++) {
		out << player.customFiles[i];

		if (i < (MAX_CUSTOM_FILES - 1)) {
			out << ", ";
		}
	}
	out << std::endl;

	out << "filesDownloaded: " << player.filesDownloaded << std::endl;
	out << "------------PLAYER------------" << std::endl;
	return out;
}

std::ostream& operator<<(std::ostream &out, const ServerInfo &serverInfo) {
	out << "------------SERVER INFO------------" << std::endl;
	out << "protocol: " << serverInfo.protocol << std::endl;
	out << "serverCount (num changeLevel): " << serverInfo.serverCount << std::endl;
	out << "isDedicated: " << ((serverInfo.isDedicated) ? "true" : "false") << std::endl;
	out << "isOfficialValveServer: " << ((serverInfo.isOfficialValveServer) ? "true" : "false") << std::endl;
	out << "isHLTV: " << ((serverInfo.isHLTV) ? "true" : "false") << std::endl;
	out << "isReplay: " << ((serverInfo.isReplay) ? "true" : "false") << std::endl;
	out << "isRedirectingToProxyRelay: " << ((serverInfo.isRedirectingToProxyRelay) ? "true" : "false") << std::endl;
	out << "cOS: " << serverInfo.cOS << std::endl;
	out << "mapCRC: " << serverInfo.mapCRC << std::endl;
	out << "clientCRC: " << serverInfo.clientCRC << std::endl;
	out << "stringTableCRC: " << serverInfo.stringTableCRC << std::endl;
	out << "maxClients: " << serverInfo.maxClients << std::endl;
	out << "maxClasses: " << serverInfo.maxClasses << std::endl;
	out << "playerSlot(current Player): " << serverInfo.playerSlot << std::endl;
	out << "tickInterval: " << serverInfo.tickInterval << std::endl;
	out << "gameDir: " << serverInfo.gameDir << std::endl;
	out << "mapName: " << serverInfo.mapName << std::endl;
	out << "mapGroupName: " << serverInfo.mapGroupName << std::endl;
	out << "skyName: " << serverInfo.skyName << std::endl;
	out << "hostName: " << serverInfo.hostName << std::endl;
	out << "publicIP: " << serverInfo.publicIP << std::endl;
	out << "ugcMapID: " << serverInfo.ugcMapID << std::endl;
	out << "------------SERVER INFO------------" << std::endl;
	return out;
}