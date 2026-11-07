#include "client.h"
#include "cl_recoil.h"
#include "recoil/model.h"

static cvar_t *activeModel, *ack;
static recoil_contract_t active;

const recoil_contract_t *CL_RecoilContract(void) { return &active; }

void CL_Recoil_Clear(void)
{
    if (!activeModel) return;
    Recoil_Decode(RECOIL_NATIVE_WIRE, &active);
    Cvar_SetInt(activeModel, 0);
    Cvar_SetString(ack, "");
}

void CL_Recoil_Init(void)
{
    cvar_t *protocol = Cvar_RegisterInt("cl_recoilProtocol", RECOIL_PROTOCOL, 0, RECOIL_PROTOCOL,
        CVAR_ROM | CVAR_USERINFO, "Recoil contract protocol (not gameplay model support)");
    cvar_t *models = Cvar_RegisterInt("cl_recoilModels", Recoil_RuntimeModels(), 0, 0x7fffffff,
        CVAR_ROM | CVAR_USERINFO, "Bitmask of integrated recoil models");
    Cvar_SetInt(protocol, RECOIL_PROTOCOL);
    Cvar_SetInt(models, Recoil_RuntimeModels());
    activeModel = Cvar_RegisterInt("cl_recoilModel", 0, 0, 31, CVAR_ROM, "Effective server recoil model");
    ack = Cvar_RegisterString("cl_recoilAck", "", CVAR_ROM | CVAR_USERINFO,
        "Acknowledged server recoil contract fingerprint");
    CL_Recoil_Clear();
}

void CL_Recoil_SystemInfoChanged(const char *systemInfo)
{
    const char *wire = Info_ValueForKey(systemInfo, "sv_recoilContract");
    recoil_contract_t contract;
    char fingerprint[17];
    if (!activeModel) return;
    if (!wire[0]) { CL_Recoil_Clear(); return; }
    if (!Recoil_Decode(wire, &contract) ||
        !Recoil_Compatible(&contract, RECOIL_PROTOCOL, Recoil_RuntimeModels())) {
        CL_Recoil_Clear();
        Com_Error(ERR_DROP, "Unsupported server recoil contract; install the matching PromodFlex client.");
        return;
    }
    Recoil_Fingerprint(wire, fingerprint);
    active = contract;
    Cvar_SetInt(activeModel, contract.model);
    Cvar_SetString(ack, fingerprint);
}
