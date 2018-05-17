#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include "pin.H"
using namespace std;


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
std::ostream * out;

/// Command line switches
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool","o", "MemoryAccess.log", "File name for MemAccessLogger output");
KNOB<BOOL>   KnobCount(KNOB_MODE_WRITEONCE, "pintool","count", "1", "Log memory accesses in the application");

/// Print out help message
INT32 Usage()
{
    cerr << "This pin tool prints out the statistics of memory accesses in an application." << endl;
    cerr << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}


/* ===================================================================== */
// Analysis Routines
/* ===================================================================== */

/* Increase counter of the executed basic blocks and instructions.
 * This function is called for every basic block when it is about to be executed.
 * @param[in]   numInstInBbl    number of instructions in the basic block
 * @note use atomic operations for multi-threaded applications
 */
VOID CountBbl(UINT32 numInstInBbl) {
    bblCount++;
    insCount += numInstInBbl;
}

VOID recordMemoryRead(ADDRINT applicationIp, ADDRINT memoryAddressRead, UINT64 memoryReadSize) {
	/*  
	printf("0x%lx %s reads %d bytes of memory at 0x%lx\n", 
		applicationIp, disAssemblyMap[applicationIp].c_str(), memoryReadSize, memoryAddressRead);
	*/	
	insCountMemAccess++;

	insCountRead++;
	memReadRecord.insert( {memoryAddressRead, {memoryReadSize,true} } );
}

VOID recordMemoryWrite(ADDRINT applicationIp, ADDRINT memoryAddressWrite, UINT64 memoryWriteSize) {
	insCountMemAccess++;	

	insCountWrite++;
	memWriteRecord.insert( {memoryAddressWrite,memoryWriteSize} );
}


/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

/* Insert call to the CountBbl() analysis routine before every basic block of the trace.
 * This function is called every time a new trace is encountered.
 * @param[in]   trace    trace to be instrumented
 * @param[in]   v        value specified by the tool in the TRACE_AddInstrumentFunction function call
 */
VOID Trace(TRACE trace, VOID *v) {
    // Visit every basic block in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to CountBbl() before every basic block, passing the number of instructions
        BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)CountBbl, IARG_UINT32, BBL_NumIns(bbl), IARG_END);
    }
}

/// Jitting-time routine
VOID Instruction(INS ins, void * v) { 
	if(INS_IsMemoryRead(ins)) {
		//disAssemblyMap[INS_Address(ins)] = INS_Disassemble(ins);
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) recordMemoryRead,
			IARG_INST_PTR, // application IP
			IARG_MEMORYREAD_EA,
			IARG_MEMORYREAD_SIZE,
			IARG_END);
	}
	
	if(INS_IsMemoryWrite(ins)) {
		//disAssemblyMap[INS_Address(ins)] = INS_Disassemble(ins);
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) recordMemoryWrite,
			IARG_INST_PTR, // application IP
			IARG_MEMORYWRITE_EA,
			IARG_MEMORYWRITE_SIZE,
			IARG_END);
	}
}


/* ===================================================================== */

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
	//*out << "addrs.size() = " << addrs.size()  << endl;	
	//*out << "memWrite.size() = " << memWrite.size() << endl; 	
	
	//////////////////////////////////////////////////////////////////////
	/// Choose a faster way for the computation 
	multimap<ADDRINT,pair<UINT64,bool>>::iterator pos;	
	if(addrs.size() < memWrite.size()) {
		// *out << "Path 1\n";	
		for(ADDRINT i : addrs) {	
			for(pos=memReadRecord.begin(); pos!=memReadRecord.end(); ++pos) {
				if( (pos->second).second == false ) { continue; }
				if( i > pos->first && i < pos->first + (pos->second).first) {	
					(pos->second).second = false;
				}
			}
		}
	} else {
		// *out << "Path 2\n";	
		for(it=memWrite.begin(); it!=memWrite.end(); ++it) {	
			for(pos=memReadRecord.begin(); pos!=memReadRecord.end(); ++pos) {
				if( (pos->second).second == false ) { continue; }
				if( isOverlap({it->first,it->second},{pos->first,(pos->second).first}) == true ) {
					(pos->second).second = false;
				}
			}
		}
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

void test();

/// Print out analysis results
VOID Fini(INT32 code, VOID *v) {
	//test();
	
	////////////////////////////////////////////////////////////////////////////////////////////////////////
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

/// When Pin was detached from the program, call detachCallback
VOID detachCallback(VOID *v) {
	*out << "Pin was detached!\n";
}


int main(int argc, char *argv[]) {
    // Initialize PIN library. Print help message if -h(elp) is specified in the command line or the command line is invalid 
    if( PIN_Init(argc,argv) ){
        return Usage();
    }
    
    string fileName = KnobOutputFile.Value();
    if(!fileName.empty()) { 
		out = new std::ofstream(fileName.c_str());
	}

    if(KnobCount) {
		// Register function to be called to instrument traces
        TRACE_AddInstrumentFunction(Trace, 0);
		// Register function to be called to instrument instructions
		INS_AddInstrumentFunction(Instruction, 0);
        // Register function to be called when the application exits
        PIN_AddFiniFunction(Fini, 0);
    }

    cerr <<  "===================================================" << endl;
    cerr <<  "This application is instrumented by MemAccessLogger" << endl;
    if (!KnobOutputFile.Value().empty()) {
        cerr << "See file " << KnobOutputFile.Value() << " for analysis results!" << endl;
    }
    cerr <<  "===================================================" << endl;
	
	// Callback functions to invoke before Pin releases control of the application
	PIN_AddDetachFunction(detachCallback, 0);

	// Start the program, never returns
    PIN_StartProgram();
    
	return 0;
}

void test() {
	/// memory read records
	multimap<ADDRINT,pair<UINT64,bool>> multimap1; 
	multimap1.insert( {1, {2,true} } );
	multimap1.insert( {1, {3,true} } ); 
	multimap1.insert( {2, {2,true} } );
	multimap1.insert( {2, {4,true} } );
	multimap1.insert( {6, {2,true} } );
	multimap1.insert( {9, {2,true} } );
	multimap1.insert( {12,{3,true} } );
	multimap1.insert( {18,{3,true} } );
	multimap1.insert( {24,{4,true} } );
	*out << "======== Memory Read Record multimap1:" << endl;
	for(multimap<ADDRINT,pair<UINT64,bool>>::iterator pos = multimap1.begin(); pos!=multimap1.end(); ++pos) {
		*out << pos->first << ":" << (pos->second).first << "  "; 
	}
	*out << "\n================================\n";

	/// memory write records
	multimap<ADDRINT,UINT64> multimap2; 
	multimap2.insert({1,3});
	multimap2.insert({5,3});
	multimap2.insert({9,1});
	multimap2.insert({13,12});
	multimap2.insert({29,2});
	*out << "======== Memory Write Record multimap2:" << endl;
	for(multimap<ADDRINT,UINT64>::iterator pos = multimap2.begin(); pos!=multimap2.end(); ++pos) {
		*out << pos->first << ":" << pos->second << " "; 
	}
	*out << "\n================================\n";

	*out << "memAccessSize = " << calMemoryAccessSize(multimap1, multimap2) << endl;
	*out << "RdOnlyMemAccessSize = " << calRdOnlyMemoryAccessSize(multimap1, multimap2) << endl;
	*out << "NumRdOnlyInstructions = " << calNumRdOnlyInstructions(multimap1, multimap2) << endl;
}

