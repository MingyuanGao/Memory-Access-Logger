#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <atomic> 
#include <thread>
#include <mutex>
#include <functional>
using namespace std;

#define ADDRINT long unsigned int
#define UINT64  long unsigned int

/// Global Variables
UINT64 insCount = 0;          //number of dynamically executed instructions
UINT64 bblCount = 0;          //number of dynamically executed basic blocks
UINT64 insCountMemAccess = 0; //number of dynamically executed instructions that access memory
UINT64 insCountRead = 0;      //number of dynamically executed instructions that read from memory
UINT64 insCountWrite = 0;     //number of dynamically executed instructions that write to memory
/* NOTE 
 * In <addr, pair<size,isReadOnly> >
 * - isReadOnly defaults to true
 * - isReadOnly is only used/updated in function "calNumRdOnlyInstructions"
 */
multimap<ADDRINT, pair<UINT64,bool> > memReadRecord; 
multimap<ADDRINT, UINT64> memWriteRecord; 
std::mutex mutexMemReadRecord;
std::ostream * out;
std::string outputFileName = "MemoryAccess.log";

/// Utility: std::string split implementation using a character as the delimiter
vector<string> split(string strToSplit, char delimeter)
{
	stringstream ss(strToSplit);
	string item;
	vector<std::string> splittedStrings;
	while(getline(ss, item, delimeter)) {
		splittedStrings.push_back(item);
	}
	
	return splittedStrings;
}

void restoreMemAccessRecords() {
	ifstream ifsMemAccess("memAccess.tmp.log");	
	ifstream ifsMemRead("memReadRecord.tmp.log");	
	ifstream ifsMemWrite("memWriteRecord.tmp.log");	
	string line;

	/// Restore outputFileName, insCount, insCountMemAccess, insCountRead, and insCountWrite 
	std::getline(ifsMemAccess,line);
	outputFileName = line; 
	std::getline(ifsMemAccess, line);
	insCount = atoi(line.c_str());
	std::getline(ifsMemAccess, line);
	insCountMemAccess = atoi(line.c_str());
	std::getline(ifsMemAccess, line);
	insCountRead = atoi(line.c_str());
	std::getline(ifsMemAccess, line);
	insCountWrite = atoi(line.c_str());
	//cerr << outputFileName << endl;
	//cerr << insCount << endl;
	//cerr << insCountMemAccess << endl;
	//cerr << insCountRead << endl;
	//cerr << insCountWrite << endl;

	vector<string> splitedStr;
	/// Restore memReadRecord 
	while( std::getline(ifsMemRead,line) ) {
		splitedStr = split(line,':');	
		memReadRecord.insert( { atoi(splitedStr[0].c_str()), { atoi(splitedStr[1].c_str()), true } } );	
		//cerr << splitedStr[0] << " " << splitedStr[1] << endl;
	}
	//cerr << memReadRecord.size() << endl;	
	
	/// Restore memWriteRecord 
	while( std::getline(ifsMemWrite,line) ) {
		splitedStr = split(line,':');	
		memWriteRecord.insert( { atoi(splitedStr[0].c_str()), atoi(splitedStr[1].c_str()) } );	
		//cerr << splitedStr[0] << " " << splitedStr[1] << endl;
	}
	//cerr << memWriteRecord.size() << endl;	
}

/// Utility: compute the size of memory accesses in memory access map where <addr1, size1> and <addr2,size2>
/// may have overlapping addresses
/*  
UINT64 calPureSize(map<ADDRINT,UINT64>& memAccess) {
	map<ADDRINT,UINT64>::iterator pos, prev;
	UINT64 memAccessSize = 0;
	
	if(memAccess.empty()) {
		return memAccessSize;	
	}

	pos = prev = memAccess.begin();
	UINT64 current_max_addr = pos->first + pos->second - 1;
	memAccessSize = pos->second;	
	pos++;
	
	for(; pos!=memAccess.end(); ++pos) {
		if(pos->first <= (prev->first + prev->second - 1) ) { 
			if( (pos->first + pos->second - 1) <= current_max_addr) {
				prev = pos;	
				continue;	
			} 
			
			memAccessSize += (pos->first + pos->second - 1) - current_max_addr; 
			current_max_addr = pos->first + pos->second - 1;	
			prev = pos;	
			continue;	
		}

		if(pos->first > current_max_addr) {
			memAccessSize += pos->second;
			current_max_addr = pos->first + pos->second - 1;	
			prev = pos;
			continue;
		} else {
			memAccessSize += (pos->first + pos->second - 1) - current_max_addr;
			current_max_addr = pos->first + pos->second - 1;
			prev = pos;	
		}
	}

	return memAccessSize;
}
*/

/// Utility: check whether <addr1,size1> and <addr2,size2> have overlapping addrs 
bool isOverlap(pair<ADDRINT,UINT64> one, pair<ADDRINT,UINT64> two) {
	ADDRINT one_begin = one.first;
	ADDRINT one_end = one.first + one.second - 1;
	ADDRINT two_begin = two.first;
	ADDRINT two_end = two.first + two.second - 1;
	
	if(one_begin > two_end || two_begin > one_end) {
		return false;
	}
	
	return true;
}

/// Utility: convert a multimap to a map, i.e., combining <addr1, size1> and 
/// <addr2, size2> where addr1 = addr2
map<ADDRINT,UINT64> convertMultimap2Map(multimap<ADDRINT,UINT64>& memAccessRecord) {
	map<ADDRINT,UINT64> ret;
	multimap<ADDRINT,UINT64>::iterator pos, prev;
	
	if(!memAccessRecord.empty()) {
		ret.insert({memAccessRecord.begin()->first, memAccessRecord.begin()->second});		

		pos = prev = memAccessRecord.begin();
		pos++;
		
		for(; pos != memAccessRecord.end(); ++pos) {
			/// case 1: pos->first == prev->first	
			if(pos->first == prev->first) {
				if(pos->second > prev->second) {
					ret.erase(prev->first);	
					ret.insert({prev->first, pos->second});	
				}	
				prev = pos;	
				continue;	
			}
			/// case 2: pos->first != prev->first	
			ret.insert({pos->first, pos->second});
			prev = pos;	
		}
	}	
	
	/*  
	*out << "==== After converted to map: "<< endl; 
	for(map<ADDRINT,UINT64>::iterator ii =ret.begin(); ii!=ret.end(); ++ii) {
		*out << ii->first << ":" << ii->second << endl; 
	}
	*out << "================" << endl; 
	*/
	
	return ret;
}

/// Compute the size of total memory accessed 
UINT64 calMemoryAccessSize(multimap<ADDRINT, pair<UINT64,bool>>& memReadRecord, multimap<ADDRINT, UINT64>& memWriteRecord) {
	std::set<ADDRINT> addrs;		
	
	multimap<ADDRINT,pair<UINT64,bool>>::iterator it;
	for(it=memReadRecord.begin(); it!=memReadRecord.end(); ++it) {
		for(ADDRINT i = it->first; i < it->first + (it->second).first; i++) {
			addrs.insert(i);				
		}
	}
	
	multimap<ADDRINT,UINT64>::iterator pos;
	for(pos=memWriteRecord.begin(); pos!=memWriteRecord.end(); ++pos) {
		for(ADDRINT i = pos->first; i < pos->first + pos->second; i++) {
			addrs.insert(i);				
		}
	}
    
	return addrs.size();
	
	/////////////////////////////////////////////////////////////////////////////////
	// If the size of memory accesses in an app is too big, use the following method, 
	// i.e., using a map, rather than a set
	/////////////////////////////////////////////////////////////////////////////////
	/* 
	multimap<ADDRINT,UINT64> memAccessRecord;

	/// Combine memReadRecord and memWriteRecord into memAccessRecord
	multimap<ADDRINT,pair<UINT64,bool>>::iterator it;
	for(it=memReadRecord.begin(); it!=memReadRecord.end();++it) {
		memAccessRecord.insert({it->first, (it->second).first});
	}
	multimap<ADDRINT,UINT64>::iterator pos;
	for(pos=memWriteRecord.begin(); pos!=memWriteRecord.end();++pos) {
		memAccessRecord.insert({pos->first, pos->second});
	}
	/// Convert memAccessRecord into a map
	map<ADDRINT,UINT64> memAccess = convertMultimap2Map(memAccessRecord); 
	
	return calPureSize(memAccess);
	*/
}

/// Compute the size of memory that are read from
UINT64 calReadMemoryAccessSize(multimap<ADDRINT, pair<UINT64,bool>>& memReadRecord) {
	std::set<ADDRINT> addrs;		
	
	multimap<ADDRINT,pair<UINT64,bool>>::iterator it;
	for(it=memReadRecord.begin(); it!=memReadRecord.end(); ++it) {
		for(ADDRINT i = it->first; i < it->first + (it->second).first; i++) {
			addrs.insert(i);				
		}
	}
	
	return addrs.size();
}

/// Compute the size of memory that are written to 
UINT64 calWriteMemoryAccessSize(multimap<ADDRINT, UINT64>& memWriteRecord) {
	std::set<ADDRINT> addrs;		
	
	multimap<ADDRINT,UINT64>::iterator pos;
	for(pos=memWriteRecord.begin(); pos!=memWriteRecord.end(); ++pos) {
		for(ADDRINT i = pos->first; i < pos->first + pos->second; i++) {
			addrs.insert(i);				
		}
	}
    
	return addrs.size();
}

/// Compute the size of memory that are ONLY read from
UINT64 calRdOnlyMemoryAccessSize(multimap<ADDRINT, pair<UINT64,bool>>& memReadRecord, multimap<ADDRINT, UINT64>& memWriteRecord) {
	std::set<ADDRINT> addrs;		
		
	multimap<ADDRINT,pair<UINT64,bool>>::iterator it;
	for(it=memReadRecord.begin(); it!=memReadRecord.end(); ++it) {
		for(ADDRINT i = it->first; i < it->first + (it->second).first; i++) {
			addrs.insert(i);				
		}
	}
	
	multimap<ADDRINT,UINT64>::iterator pos;
	for(pos=memWriteRecord.begin(); pos!=memWriteRecord.end(); ++pos) {
		for(ADDRINT i = pos->first; i < pos->first + pos->second; i++) {
			addrs.erase(i);				
		}
	}

	return addrs.size();
}

/// Utility: distribute the computation in calNumRdOnlyInstructions to all CPU cores
void thread_func_vector(std::set<ADDRINT>& addrs) {
	multimap<ADDRINT,pair<UINT64,bool>>::iterator pos;	
	
	//cout << addrs.size() << endl;	
	
	for(ADDRINT i : addrs) {	
		for(pos=memReadRecord.begin(); pos!=memReadRecord.end(); ++pos) {
			if( (pos->second).second == false ) { continue; }
			if( i > pos->first && i < pos->first + (pos->second).first) {	
				std::lock_guard<std::mutex> lk(mutexMemReadRecord);
				(pos->second).second = false;
			}
		}
	}
} 

/// Utility: distribute the computation in calNumRdOnlyInstructions to all CPU cores
void thread_func_map(map<ADDRINT,UINT64>& memWrite) {
	map<ADDRINT,UINT64>::iterator it;
	multimap<ADDRINT,pair<UINT64,bool>>::iterator pos;	
	
	//cout << "memWrite.size() = " << memWrite.size() << endl;	

	for(it=memWrite.begin(); it!=memWrite.end(); ++it) {	
		for(pos=memReadRecord.begin(); pos!=memReadRecord.end(); ++pos) {
			if( (pos->second).second == false ) { continue; }
			if( isOverlap({it->first,it->second},{pos->first,(pos->second).first}) == true ) {
				std::lock_guard<std::mutex> lk(mutexMemReadRecord);
				(pos->second).second = false;
			}
		}
	}
}

/// Compute the number of instructions that ONLY read from memory 
UINT64 calNumRdOnlyInstructions(multimap<ADDRINT, pair<UINT64,bool>>& memReadRecord, multimap<ADDRINT, UINT64>& memWriteRecord) {
	map<ADDRINT,UINT64> memWrite = convertMultimap2Map(memWriteRecord);	

	std::set<ADDRINT> addrs;		
	map<ADDRINT,UINT64>::iterator it;
	for(it=memWrite.begin(); it!=memWrite.end(); ++it) {
		for(ADDRINT i = it->first; i < it->first + it->second; i++) {
			addrs.insert(i);
		}
	}	
	//cout << "addrs.size() = " << addrs.size()  << endl;	
	//cout << "memWrite.size() = " << memWrite.size() << endl; 	
	
	//////////////////////////////////////////////////////////////////////
	vector<std::thread> threads; 
	int num_cpus = std::thread::hardware_concurrency();
	multimap<ADDRINT,pair<UINT64,bool>>::iterator pos;	

	/// Choose a faster way for the computation
	if(addrs.size() < memWrite.size()) {
		vector<set<ADDRINT>> addr_groups;
		
		///TODO: find a way to do this automaticaly
		set<ADDRINT> s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15, s16, s17, s18, s19, s20, s21, s22, s23; 	
		int set_num;
		for(ADDRINT i : addrs) {
			set_num = i % 24;
			switch(set_num) {
				case  0:  s0.insert(i); break;  
				case  1:  s1.insert(i); break;  
				case  2:  s2.insert(i); break;  
				case  3:  s3.insert(i); break;  
				case  4:  s4.insert(i); break;  
				case  5:  s5.insert(i); break;  
				case  6:  s6.insert(i); break;  
				case  7:  s7.insert(i); break;  
				case  8:  s8.insert(i); break;  
				case  9:  s9.insert(i); break;  
				case 10: s10.insert(i); break;  
				case 11: s11.insert(i); break;  
				case 12: s12.insert(i); break;  
				case 13: s13.insert(i); break;  
				case 14: s14.insert(i); break;  
				case 15: s15.insert(i); break;  
				case 16: s16.insert(i); break;  
				case 17: s17.insert(i); break;  
				case 18: s18.insert(i); break;  
				case 19: s19.insert(i); break;  
				case 20: s20.insert(i); break;  
				case 21: s21.insert(i); break;  
				case 22: s22.insert(i); break;  
				case 23: s23.insert(i); break;  
			}
		}
		addr_groups.push_back( s0);
		addr_groups.push_back( s1);
		addr_groups.push_back( s2);
		addr_groups.push_back( s3);
		addr_groups.push_back( s4);
		addr_groups.push_back( s5);
		addr_groups.push_back( s6);
		addr_groups.push_back( s7);
		addr_groups.push_back( s8);
		addr_groups.push_back( s9);
		addr_groups.push_back(s10);
		addr_groups.push_back(s11);
		addr_groups.push_back(s12);
		addr_groups.push_back(s13);
		addr_groups.push_back(s14);
		addr_groups.push_back(s15);
		addr_groups.push_back(s16);
		addr_groups.push_back(s17);
		addr_groups.push_back(s18);
		addr_groups.push_back(s19);
		addr_groups.push_back(s20);
		addr_groups.push_back(s21);
		addr_groups.push_back(s22);
		addr_groups.push_back(s23);
		
		//cout << "addr_groups.size() = " << addr_groups.size() << endl;
		//addr_groups.push_back(addrs);
		for(auto& s : addr_groups) {
			threads.push_back( std::thread{ thread_func_vector, std::ref(s) } ); 
		}
		for(auto& t : threads) { t.join(); }
	} else {
		vector<map<ADDRINT,UINT64>> memWrite_groups;
		
		///TODO: find a way to do this automaticaly
		map<ADDRINT,UINT64> m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15, m16, m17, m18, m19, m20, m21, m22, m23;	
		UINT64 counter = 0;	
		int map_num = 0;
		for(map<ADDRINT,UINT64>::iterator it = memWrite.begin(); it!=memWrite.end(); ++it) {
			map_num = counter++ % 24;
			switch(map_num) {
				case  0:  m0.insert( {it->first, it->second} ); break;  
				case  1:  m1.insert( {it->first, it->second} ); break;  
				case  2:  m2.insert( {it->first, it->second} ); break;  
				case  3:  m3.insert( {it->first, it->second} ); break;  
				case  4:  m4.insert( {it->first, it->second} ); break;  
				case  5:  m5.insert( {it->first, it->second} ); break;  
				case  6:  m6.insert( {it->first, it->second} ); break;  
				case  7:  m7.insert( {it->first, it->second} ); break;  
				case  8:  m8.insert( {it->first, it->second} ); break;  
				case  9:  m9.insert( {it->first, it->second} ); break;  
				case 10: m10.insert( {it->first, it->second} ); break;  
				case 11: m11.insert( {it->first, it->second} ); break;  
				case 12: m12.insert( {it->first, it->second} ); break;  
				case 13: m13.insert( {it->first, it->second} ); break;  
				case 14: m14.insert( {it->first, it->second} ); break;  
				case 15: m15.insert( {it->first, it->second} ); break;  
				case 16: m16.insert( {it->first, it->second} ); break;  
				case 17: m17.insert( {it->first, it->second} ); break;  
				case 18: m18.insert( {it->first, it->second} ); break;  
				case 19: m19.insert( {it->first, it->second} ); break;  
				case 20: m20.insert( {it->first, it->second} ); break;  
				case 21: m21.insert( {it->first, it->second} ); break;  
				case 22: m22.insert( {it->first, it->second} ); break;  
				case 23: m23.insert( {it->first, it->second} ); break;  
			}
		}
		memWrite_groups.push_back( m0);
		memWrite_groups.push_back( m1);
		memWrite_groups.push_back( m2);
		memWrite_groups.push_back( m3);
		memWrite_groups.push_back( m4);
		memWrite_groups.push_back( m5);
		memWrite_groups.push_back( m6);
		memWrite_groups.push_back( m7);
		memWrite_groups.push_back( m8);
		memWrite_groups.push_back( m9);
		memWrite_groups.push_back(m10);
		memWrite_groups.push_back(m11);
		memWrite_groups.push_back(m12);
		memWrite_groups.push_back(m13);
		memWrite_groups.push_back(m14);
		memWrite_groups.push_back(m15);
		memWrite_groups.push_back(m16);
		memWrite_groups.push_back(m17);
		memWrite_groups.push_back(m18);
		memWrite_groups.push_back(m19);
		memWrite_groups.push_back(m20);
		memWrite_groups.push_back(m21);
		memWrite_groups.push_back(m22);
		memWrite_groups.push_back(m23);
	
		//cout << "memWrite_groups.size() = " << memWrite_groups.size() << endl;
		//memWrite_groups.push_back(memWrite);	
		for(auto& m : memWrite_groups) {
			threads.push_back( std::thread{ thread_func_map, std::ref(m) } ); 
		}
		for(auto& t : threads) { t.join(); }
	}
	//////////////////////////////////////////////////////////////////////
	
	UINT64 ret = 0;
	for(pos=memReadRecord.begin(); pos!=memReadRecord.end(); ++pos) {
		if( (pos->second).second == true) {
			ret++;
		}
	}
	
	return ret;
}


/// Print out analysis results
void Fini( ) {
	UINT64 insCountReadOnly = calNumRdOnlyInstructions(memReadRecord,memWriteRecord); 
	*out << "MemoryAccessLogger Results: \n";
	*out << "============================================================\n\n";
    *out << "Number of instructions executed        : " << insCount  << "\n";
    *out << "----------------------------------------------------------\n";
    *out << "Number of memory-access instructions   : " << insCountMemAccess << " (" 
		 << ((float)insCountMemAccess/(float)insCount * 100 ) << "%) \n";
	*out << "    Number of memory-read instructions : " << insCountRead << "\n" 
		 << "        " << ((float)insCountRead/(float)insCount * 100 )  << "% of all instructions\n"
		 << "        " << ((float)insCountRead/(float)insCountMemAccess* 100 ) << "% of memory-access instructions\n";
   	*out << "    Number of memory-write instructions: " << insCountWrite << "\n" 
		 << "        " << ((float)insCountWrite/(float)insCount * 100 )  << "% of all instructions\n" 
		 << "        " << ((float)insCountWrite/(float)insCountMemAccess* 100 )  << "% of memory-access instructions\n";
	*out << "    Number of instructions that ONLY read from memory: " << insCountReadOnly << "\n" 
		 << "        " << ((float)insCountReadOnly/(float)insCountRead * 100 )  << "% of memory-read instructions\n" 
		 << "        " << ((float)insCountReadOnly/(float)insCountMemAccess * 100 )  << "% of memory-access instructions\n\n";

	UINT64 memAccessSize = calMemoryAccessSize(memReadRecord,memWriteRecord)/1024;
	UINT64 memAccessSizeRead = calReadMemoryAccessSize(memReadRecord)/1024;
	UINT64 memAccessSizeWrite = calWriteMemoryAccessSize(memWriteRecord)/1024;
	UINT64 memAccessSizeRdOnly = calRdOnlyMemoryAccessSize(memReadRecord,memWriteRecord)/1024; 
	*out << "Total Memory Accessed               : " << memAccessSize  << "KB \n";
    *out << "----------------------------------------------------------\n";
	*out << "   Size of memory that are read from: " << memAccessSizeRead << "KB (" 
		 << ( (float)memAccessSizeRead/(float)memAccessSize) * 100 << "%) \n";
	*out << "   Size of memory that are written to: " << memAccessSizeWrite << "KB (" 
		 << ( (float)memAccessSizeWrite/(float)memAccessSize) * 100 << "%) \n";
	*out << "   Size of memory that are ONLY read from: " << memAccessSizeRdOnly << "KB (" 
		 << ( (float)memAccessSizeRdOnly/(float)memAccessSize) * 100 << "%) \n";
	*out << "============================================================" << endl;
}


int main(int argc, char *argv[]) {
	// Restore memReadRecord and memWriteRecord
	restoreMemAccessRecords();
	
	out = new std::ofstream(outputFileName);
    cout << "Analyzing ..." << endl << "(Be patient, this may take hours!)" << endl;
	Fini();
    cout << "See file " << outputFileName << " for analysis results!" << endl;
	
	return 0;
}

