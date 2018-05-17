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

void saveMemAccessRecords() {
	std::ofstream ofsMemAccess("memAccess.tmp.log");
	string fileName = KnobOutputFile.Value();
    if(!fileName.empty()) { 
		ofsMemAccess << fileName.c_str() << "\n";
	} else {
		ofsMemAccess << "MemoryAccess.log" << "\n";
	}
	ofsMemAccess << insCount << "\n";      
    ofsMemAccess << insCountMemAccess << "\n";
    ofsMemAccess << insCountRead << "\n";
    ofsMemAccess << insCountWrite << "\n";     
	
	multimap<ADDRINT,pair<UINT64,bool>>::iterator pos;	
	std::ofstream ofsReadRecord("memReadRecord.tmp.log");
	for(pos=memReadRecord.begin(); pos!=memReadRecord.end(); ++pos) {
		ofsReadRecord << pos->first << ":" << (pos->second).first << "\n";
	}

	multimap<ADDRINT,UINT64>::iterator it;	
	std::ofstream ofsWriteRecord("memWriteRecord.tmp.log");
	for(it=memWriteRecord.begin(); it!=memWriteRecord.end(); ++it) {
		ofsWriteRecord << it->first << ":" << it->second << "\n";
	}
}

/// Print out analysis results
VOID Fini(INT32 code, VOID *v) {
	//test();
	saveMemAccessRecords();	
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

