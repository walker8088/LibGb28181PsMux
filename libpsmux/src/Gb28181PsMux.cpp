#include "../inc/Gb28181PsMux.h"

#include<string.h>
#include<stdio.h>

#include "psmux.h"


//处理具有同一个时间戳的多个帧,比如SPS PPS I帧
struct MuxMultiFrameContext 
{
    MuxMultiFrameContext():Idx(-1),
                            OutBuf(NULL),
                            MaxOutSize(0),
                            OutSize(0),
                            pMux(NULL)
    {
    }
    virtual int MuxOneOfMultiFrame(guint8* buf, int len) = 0;
    StreamIdx Idx;
    guint8* OutBuf;
    int MaxOutSize;
    gint64 pts;
    gint64 dts;
    Gb28181PsMux* pMux;
protected:
    int OutSize;
};

int MuxBlock(unsigned char* buf, int len, int MaxSlice, MuxMultiFrameContext* pContext);

Gb28181PsMux::Gb28181PsMux():m_PsMuxContext(NULL)
{
	m_SpsPpsIBuf = new guint8[MAX_SPSPPSI_SIZE];
	m_SpsPpsIBufSize = 0;
}

Gb28181PsMux::~Gb28181PsMux()
{
    if (m_PsMuxContext){
        psmux_free(m_PsMuxContext);
    }

	delete []m_SpsPpsIBuf;
}

struct MuxH265VpsSpsPpsIFrameContext : public MuxMultiFrameContext
{
public:
    virtual int MuxOneOfMultiFrame(guint8* buf, int len)
    {
        int r = pMux->MuxH265SingleFrame(buf, len, pts, dts, Idx, OutBuf, &OutSize, MaxOutSize);
        
        if (r == MUX_WAIT){
            return MUX_OK;
        }

        if (r == MUX_ERROR || r == MEM_ERROR){
            return r;
        }

        OutBuf += OutSize;
        MaxOutSize -= OutSize;
        return MUX_OK;
    }
};

struct MuxH264SPSPPSIFrameContext : public MuxMultiFrameContext
{
public:
    virtual int MuxOneOfMultiFrame(guint8* buf, int len)
    {
        int r = pMux->MuxH264SingleFrame(buf, len, pts, dts, Idx, OutBuf, &OutSize, MaxOutSize);

        if (r == MUX_WAIT){
            return MUX_OK;
        }
        
        if (r == MUX_ERROR || r == MEM_ERROR){
            return r;
        }

        OutBuf += OutSize;
        MaxOutSize -= OutSize;
        return MUX_OK;
    }
};

StreamIdx Gb28181PsMux::AddStream(PsMuxStreamType Type)
{
    if(m_PsMuxContext == NULL){
        m_PsMuxContext = psmux_new();
    }
    PsMuxStream * pStream = psmux_create_stream (m_PsMuxContext, Type);
    m_VecStream.push_back(pStream);
    return (StreamIdx)(m_VecStream.size()-1);
}

int Gb28181PsMux::MuxH265VpsSpsPpsIFrame(guint8* buf, int len, gint64 Pts, gint64 Dts, StreamIdx Idx,
                                         guint8 * outBuf, int* pOutSize, int maxOutSize)
{
    MuxH265VpsSpsPpsIFrameContext context;
    context.Idx = Idx;
    context.pts = Pts;
    context.dts = Dts;
    context.OutBuf = outBuf;
    context.MaxOutSize = maxOutSize;
    context.pMux = this;
    MuxBlock(buf, len, 4, &context);
    if (pOutSize){//计算用去了多少字节
        *pOutSize = context.OutBuf - outBuf;
    }
    return MUX_OK;
}

int Gb28181PsMux::MuxH264SpsPpsIFrame(guint8* buf, int len, gint64 Pts, gint64 Dts, StreamIdx Idx,
                                      guint8 * outBuf, int* pOutSize, int maxOutSize)
{
    MuxH264SPSPPSIFrameContext context;
    context.Idx = Idx;
    context.pts = Pts;
    context.dts = Dts;
    context.OutBuf = outBuf;
    context.MaxOutSize = maxOutSize;
    context.pMux = this;
    MuxBlock(buf, len, 3, &context);
    if (pOutSize){//计算用去了多少字节
        *pOutSize = context.OutBuf - outBuf;
    }
    return MUX_OK;
}

int Gb28181PsMux::MuxH264SingleFrame(guint8* buf, int len, gint64 Pts, gint64 Dts, StreamIdx Idx,
                                     guint8 * outBuf, int* pOutSize, int maxOutSize)
{
	*pOutSize = 0;

    if (Idx >= m_VecStream.size()){
        return MUX_ERROR;
    }

    unsigned char c = 0;
    if (!isH264Or265Frame(buf, &c)){
        return MUX_ERROR;
    }

    NAL_type Type = getH264NALtype(c);
    
    if (Type == NAL_other){
        return MUX_ERROR;
    }

    if (Type == NAL_SEI){
        return MUX_OK;
    }

    PsMuxStream * pMuxStream = m_VecStream[Idx];

    //default
    m_PsMuxContext->enable_pack_hdr = 0;
    m_PsMuxContext->enable_psm = 0;
    m_PsMuxContext->enable_sys_hdr = 0;
    pMuxStream->pi.flags &= ~PSMUX_PACKET_FLAG_PES_DATA_ALIGN;
    m_PsMuxContext->pts = Pts;

    if (Pts == Dts){
        Dts = INVALID_TS;
    }

    if (Type == NAL_PFRAME){
        m_PsMuxContext->enable_pack_hdr = 1;
        pMuxStream->pi.flags |= PSMUX_PACKET_FLAG_PES_DATA_ALIGN;
		int r = psmux_mux_frame(m_PsMuxContext, m_VecStream[Idx], buf, len, Pts, Dts, outBuf, pOutSize, maxOutSize);
        if (r == MUX_ERROR || r == MEM_ERROR){
            return r;
        }
    }
    else {
		//如果是单个SPS PPS 则等到I帧一起发送,原则就是同一个时间戳作为一个RTP包
		if (Type == NAL_SPS){
	        m_PsMuxContext->enable_pack_hdr = 1;
	        m_PsMuxContext->enable_psm = 1;
	        m_PsMuxContext->enable_sys_hdr = 1;
	        Pts = INVALID_TS;
	        Dts = INVALID_TS;
	    }
	    else if (Type == NAL_PPS){
	        Pts = INVALID_TS;
	        Dts = INVALID_TS;
	    }
		else if (Type == NAL_IDR){
        	pMuxStream->pi.flags |= PSMUX_PACKET_FLAG_PES_DATA_ALIGN;
    	}

		int outSize = 0;
		psmux_mux_frame(m_PsMuxContext, m_VecStream[Idx], buf, len, Pts, Dts, 
			m_SpsPpsIBuf+m_SpsPpsIBufSize, &outSize, MAX_SPSPPSI_SIZE-m_SpsPpsIBufSize);
		m_SpsPpsIBufSize += outSize;

		if (Type == NAL_IDR){
			if(m_SpsPpsIBufSize > maxOutSize){
				return MEM_ERROR;
			}

			memcpy(outBuf, m_SpsPpsIBuf, m_SpsPpsIBufSize);
			*pOutSize = m_SpsPpsIBufSize;
			m_SpsPpsIBufSize = 0;
		}
		else{
			return MUX_WAIT;
		}
		
		return MUX_OK;
	}
	
    return MUX_OK;
}

int Gb28181PsMux::MuxH265SingleFrame(guint8* buf, int len, gint64 Pts, gint64 Dts, StreamIdx Idx,
                       guint8 * outBuf, int* pOutSize, int maxOutSize)
{
    *pOutSize = 0;

    if (Idx >= m_VecStream.size()){
        return MUX_ERROR;
    }

    PsMuxStream * pMuxStream = m_VecStream[Idx];

    unsigned char c = 0;
    if (!isH264Or265Frame(buf, &c)){
        return MUX_ERROR;
    }

    NAL_type Type = getH265NALtype(buf[4]);
    if (Type == NAL_other){
        return MUX_ERROR;
    }
    
    if (Type == NAL_SEI){
        return MUX_OK;
    }

    //default
    {
        m_PsMuxContext->enable_pack_hdr = 0;
        m_PsMuxContext->enable_psm = 0;
        m_PsMuxContext->enable_sys_hdr = 0;
    }

    pMuxStream->pi.flags |= PSMUX_PACKET_FLAG_PES_DATA_ALIGN;

    if (Type == NAL_PFRAME){
        m_PsMuxContext->enable_pack_hdr = 1;
        int r = psmux_mux_frame(m_PsMuxContext, m_VecStream[Idx], buf, len, Pts, Dts, outBuf, pOutSize, maxOutSize);
        if (r == MUX_ERROR || r == MEM_ERROR){
            return r;
        }
    }
    else{
        if (Type == NAL_VPS){
            m_PsMuxContext->enable_pack_hdr = 1;
            m_PsMuxContext->enable_psm = 1;
            m_PsMuxContext->enable_sys_hdr = 1;
        }
        else if (Type == NAL_PPS){
            m_PsMuxContext->enable_pack_hdr = 1;
        }

        int outSize = 0;
        psmux_mux_frame(m_PsMuxContext, m_VecStream[Idx], buf, len, Pts, Dts, 
            m_SpsPpsIBuf+m_SpsPpsIBufSize, &outSize, MAX_SPSPPSI_SIZE-m_SpsPpsIBufSize);
        m_SpsPpsIBufSize += outSize;

        if (Type == NAL_IDR){
            if(m_SpsPpsIBufSize > maxOutSize){
                return MEM_ERROR;
            }

            memcpy(outBuf, m_SpsPpsIBuf, m_SpsPpsIBufSize);
            *pOutSize = m_SpsPpsIBufSize;
            m_SpsPpsIBufSize = 0;
        }
        else{
            return MUX_WAIT;
        }
    }

    return MUX_OK;
}

int Gb28181PsMux::MuxAudioFrame(guint8* buf, int len, gint64 Pts, gint64 Dts, StreamIdx Idx,
                  guint8 * outBuf, int* pOutSize, int maxOutSize)
{
    if (Idx >= m_VecStream.size()){
        return 1;
    }

    PsMuxStream * pMuxStream = m_VecStream[Idx];

    m_PsMuxContext->enable_pack_hdr = 0;
    m_PsMuxContext->enable_psm = 0;
    m_PsMuxContext->enable_sys_hdr = 0;

    pMuxStream->pi.flags |= PSMUX_PACKET_FLAG_PES_DATA_ALIGN;

    psmux_mux_frame(m_PsMuxContext, m_VecStream[Idx], buf, len, Pts, INVALID_TS, outBuf, pOutSize, maxOutSize);
    return MUX_OK;
}

//遍历block拆分NALU,直到MaxSlice,不然一直遍历下去
int MuxBlock(guint8* pBlock, int BlockLen, int MaxSlice, MuxMultiFrameContext* pContext)
{
    guint8* pCurPos = pBlock;
    int LastBlockLen = BlockLen;

    guint8* NaluStartPos = NULL;

    if(pContext == NULL) return MUX_ERROR;

    //一段数据里最多NALU个数,这样SPS PPS 后的I帧那就不用遍历
    int iSliceNum = 0;

    while (LastBlockLen > 4)
    {
        if(isH264Or265Frame(pCurPos, NULL)){

            iSliceNum++;
     
            if (NaluStartPos == NULL){
                NaluStartPos = pCurPos;
            }
            else{
                pContext->MuxOneOfMultiFrame(NaluStartPos, pCurPos-NaluStartPos);
                NaluStartPos = pCurPos;
            }

            if (iSliceNum >= MaxSlice){//已经到达最大NALU个数,下面的不用找了把剩下的加上就是
                pContext->MuxOneOfMultiFrame(pCurPos, LastBlockLen);
                break;
            }
        }
        
        pCurPos++;
        LastBlockLen--;
    }

    return MUX_OK;
}

//判断是否是264或者265帧,如果是顺便把NalTypeChar设置一下
bool isH264Or265Frame(guint8* buf, unsigned char* NalTypeChar)
{
    bool bOk = false;
    unsigned char c = 0;

    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] == 1){
        if (NalTypeChar){
            *NalTypeChar = buf[4];
        }
        bOk = true;
    }

    if (buf[0] == 0 && buf[1] == 0 && buf[2] == 1 ){

        if (NalTypeChar){
            *NalTypeChar = buf[3];
        }

        bOk = true;
    }

    return bOk;
}

NAL_type getH264NALtype(guint8 c)
{
    switch(c & 0x1f){
        case 6:
            return NAL_SEI;
            break;
        case 7:
            return NAL_SPS;
            break;
        case 8:
            return NAL_PPS;
            break;
        case 5:
            return NAL_IDR;
            break;
        case 1:
            return NAL_PFRAME;
            break;
        default:
            return NAL_other;
            break;
    }
    return NAL_other;
}

NAL_type getH265NALtype(guint8 c)
{
    int type = (c & 0x7E)>>1;

    if(type == 33)
        return NAL_SPS;

    if(type == 34)
        return NAL_PPS;

    if(type == 32)
        return NAL_VPS;

    if(type == 39)
        return NAL_SEI_PREFIX;

    if(type == 40)
        return NAL_SEI_SUFFIX;

    if((type >= 1) && (type <=9))
        return NAL_PFRAME;

    if((type >= 16) && (type <=21))
        return NAL_IDR;

    return NAL_other;
}
