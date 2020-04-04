//! \file RzChromaBroadcastAPITypes.h
//! \brief Data types.

#ifndef _RZCHROMABROADCASTAPITYPES_H_
#define _RZCHROMABROADCASTAPITYPES_H_

#pragma once

typedef LONG            RZRESULT;           //!< Return result.
typedef LONG            RZSTATUS;           //!< Status
typedef GUID            RZEFFECTID;         //!< Effect Id.
typedef GUID            RZDEVICEID;         //!< Device Id.
typedef GUID            RZAPPID;            //!< Application Id.
typedef unsigned int    RZDURATION;         //!< Milliseconds.
typedef size_t          RZSIZE;             //!< Size.
typedef void*           PRZPARAM;           //!< Context sensitive pointer.
typedef DWORD           RZID;               //!< Generic data type for Identifier.
typedef DWORD           RZCOLOR;            //!< Color data. 1st byte = Red; 2nd byte = Green; 3rd byte = Blue; 4th byte = Alpha (if applicable)

namespace RzChromaBroadcastAPI
{
	enum CHROMA_BROADCAST_TYPE : BYTE
	{
		BROADCAST_EFFECT = 1,
		BROADCAST_STATUS = 2,
	};

	enum CHROMA_BROADCAST_STATUS
	{
		LIVE = 1,
		NOT_LIVE = 2,
	};

#pragma pack(push, 1)
	struct CHROMA_BROADCAST_EFFECT
	{
		RZCOLOR CL1;
		RZCOLOR CL2;
		RZCOLOR CL3;
		RZCOLOR CL4;
		RZCOLOR CL5;
		BOOL IsAppSpecific;
	};
#pragma pack(pop)

	typedef RZRESULT(*RZEVENTNOTIFICATIONCALLBACK)(CHROMA_BROADCAST_TYPE type, PRZPARAM pData);
}

#endif
