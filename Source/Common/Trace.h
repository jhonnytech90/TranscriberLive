#pragma once

#include "Log.h"

/*
    Trace.h agora e so uma casca sobre Log.h.

    Antes daqui saia um rastreio proprio, que abria e fechava o arquivo a cada
    linha. Isso servia para as poucas etapas de inicializacao, mas nao para
    logar a sessao inteira — uma abertura de arquivo por linha derrubaria o
    desempenho. O motor de verdade e o tl::Log; estes macros continuam
    existindo para nao quebrar as chamadas antigas.
*/

#define TL_TRACE(msg)       TL_LOGI ("init", msg)
#define TL_TRACE_STEP(nome) TL_LOG_STEP (nome)
