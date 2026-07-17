#ifndef EMP_IR_IR_H__
#define EMP_IR_IR_H__

// ir — the context-free Boolean IR and execution contracts: BooleanProgram and its
// validation / visiting / passes / scheduling / execution, the .empbc assets /
// builtins / artifacts, the reusable non-protocol contexts (the BooleanContext /
// BulkBooleanContext concepts + ClearCtx / CountCtx / DigestCtx / RecordCtx), the
// generic WireBundle/WireValue concepts, and the generic session contracts (Session /
// DirectSession / SessionIO / CheckpointingSession + the plaintext ClearSession).
// It builds on runtime and is built on by circuits.

#include "emp-tool/ir/program.h"
#include "emp-tool/ir/validate.h"
#include "emp-tool/ir/visit.h"
#include "emp-tool/ir/passes.h"
#include "emp-tool/ir/analysis.h"
#include "emp-tool/ir/execute.h"
#include "emp-tool/ir/schedule.h"
#include "emp-tool/ir/artifact.h"
#include "emp-tool/ir/empbc.h"
#include "emp-tool/ir/assets.h"
#include "emp-tool/ir/builtins.h"
#include "emp-tool/ir/wire_value.h"
#include "emp-tool/ir/context/context.h"
#include "emp-tool/ir/session/session.h"
#include "emp-tool/ir/session/session_io.h"
#include "emp-tool/ir/session/clear_session.h"

#endif  // EMP_IR_IR_H__
