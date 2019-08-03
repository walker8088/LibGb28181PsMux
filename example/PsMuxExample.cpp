

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <string>

#include <faac.h>

#include "Gb28181PsMux.h"

#define BUF_LEN (1024*1024)


class FaacEncoder
{
public:
    FaacEncoder()
    {
        aac_encoder = faacEncOpen(8000, 1, &input_samples, &max_output_bytes);
        faacEncConfigurationPtr aac_configuration = faacEncGetCurrentConfiguration(aac_encoder);
        aac_configuration->inputFormat = FAAC_INPUT_16BIT;
        aac_configuration->mpegVersion = MPEG2;
        aac_configuration->outputFormat = ADTS_STREAM;
        faacEncSetConfiguration(aac_encoder, aac_configuration);
        out_buff = new uint8_t[max_output_bytes];
    }

    ~FaacEncoder()
    {
        delete out_buff;
    }
    
    int encode(uint8_t* input_buff, int buff_len)
    {
          return  faacEncEncode(aac_encoder, (int32_t*)input_buff, 1024, out_buff, max_output_bytes);
    }

public:
   uint8_t*  out_buff;
   unsigned long  input_samples;
   unsigned long  max_output_bytes;
    
private:
   faacEncHandle aac_encoder;

};

FaacEncoder* paac_encoder; 
FILE *pcm_file;
uint8_t pcm_buff[2048];

struct PsMuxContext 
{
    PsMuxContext()
    {
        Idx = PsMux.AddStream(PSMUX_ST_VIDEO_H264);
        Idx_a =  PsMux.AddStream(PSMUX_ST_AUDIO_AAC);
        pMuxBuf = new guint8[BUF_LEN];
        pts = 0;
        pts_a = 0;
    }
    ~PsMuxContext()
    {
        delete []pMuxBuf;
    }

    virtual void ProcessVideo(guint8* buf, int len)
    {
        int MuxOutSize = 0;
        int ret = PsMux.MuxH264SingleFrame(buf, len, pts, pts, Idx, pMuxBuf, &MuxOutSize, BUF_LEN);
        
        if (ret == 0 && MuxOutSize > 0){
            OnPsFrameOut(pMuxBuf, MuxOutSize, pts, pts);
        }
        else if(ret == 3){
           //buffered data
        }
        else{
            printf("mux error!\n");
        }

        unsigned char c = 0;
        if (!isH264Or265Frame(buf, &c)){
            return;
        }

        NAL_type Type = getH264NALtype(c);
        int pcm_len, aac_len;

        if ((Type == NAL_IDR) || (Type == NAL_PFRAME)){
            pts += 3600;
            while (pts > pts_a){
                pcm_len = fread(pcm_buff, 2018, 1, pcm_file);
                if(pcm_len > 0){
                    aac_len = paac_encoder->encode(pcm_buff, 1024);
                    if(aac_len>0){
                        ret = PsMux.MuxAudioFrame(paac_encoder-> out_buff, aac_len, pts_a, pts_a, Idx_a, pMuxBuf, &MuxOutSize, BUF_LEN);
                        if (ret == 0 && MuxOutSize > 0){
                             OnPsFrameOut(pMuxBuf, MuxOutSize, pts, pts);
                             pts_a += 1024;
                        }
                    }    
                }
                else{
                    break;
                }
            }    
        }

    }
    
    virtual void ProcessAudio(guint8* buf, int len)
    {
        int MuxOutSize = 0;
        
        int ret = PsMux.MuxAudioFrame(buf, len, pts_a, pts_a, Idx, pMuxBuf, &MuxOutSize, BUF_LEN);
        
        if (ret == 0 && MuxOutSize > 0){
            OnPsFrameOut(pMuxBuf, MuxOutSize, pts_a, pts_a);
        }
        else if(ret == 3){
           //uffered data
        }
        else{
            printf("mux error!\n");
        }

        pts_a += 11520;
        
    }
    
    void testMuxSpsPpsI(guint8* buf, int len)
    {
        int MuxOutSize = 0;
        PsMux.MuxH264SpsPpsIFrame(buf, len, 0, 0, Idx, pMuxBuf, &MuxOutSize, BUF_LEN);
    }

    virtual void OnPsFrameOut(guint8* buf, int len, gint64 pts, gint64 dts) = 0;

private:
    Gb28181PsMux PsMux;
    StreamIdx Idx;
    StreamIdx Idx_a;
    guint8* pMuxBuf;
    gint64 pts;
    gint64 pts_a;
      
};

struct PsProcessSaveFile : public PsMuxContext
{
    PsProcessSaveFile(std::string DstName)
    {
        fp = fopen(DstName.c_str(), "wb+");
    }
    ~PsProcessSaveFile()
    {
        if (fp){
            fclose(fp);
        }
    }
    virtual void OnPsFrameOut(guint8* buf, int len, gint64 pts, gint64 dts)
    {
        if (len > 0 && fp)
        {
            fwrite(buf, len, 1, fp);
            fflush(fp);
        }
    }
    FILE* fp;
};


//遍历block拆分NALU,直到MaxSlice,不然一直遍历下去
int process_block(guint8* pBlock, int BlockLen, int MaxSlice,  PsMuxContext* PsDst)
{
    static guint8* pStaticBuf = new guint8[BUF_LEN];
    static int StaticBufSize = 0;

    guint8* pCurBlock = NULL;

    int LastBlockLen = 0;

    memcpy(pStaticBuf+StaticBufSize, pBlock, BlockLen);

    LastBlockLen = StaticBufSize+BlockLen;

    guint8* pCurPos = pStaticBuf;

    guint8* NaluStartPos = NULL;
    guint8* NaluEndPos   = NULL;


    //一段数据里最多NALU个数,这样SPS PPS 后的I帧那就不用遍历
    int iSliceNum = 0;

    while (LastBlockLen > 4)
    {
        if(isH264Or265Frame(pCurPos,NULL)){
            if (iSliceNum + 1 >= MaxSlice){//已经到达最大NALU个数,下面的不用找了把剩下的加上就是
                PsDst->ProcessVideo(pCurPos, LastBlockLen);
                break;
            }

            if (NaluStartPos == NULL){
                NaluStartPos = pCurPos;
            }
            else{
                PsDst->ProcessVideo(NaluStartPos, pCurPos-NaluStartPos);
                iSliceNum++;
                NaluStartPos = pCurPos;
            }
        }

        pCurPos++;
        LastBlockLen--;
    }

    //有剩下的,保存,和后面的拼起来
    if (NaluStartPos){
        memcpy(pStaticBuf, NaluStartPos, LastBlockLen);
        StaticBufSize = LastBlockLen;
    }
    return 0;
}


int main(int argc, char* argv[])
{
    Gb28181PsMux PsMuxer;
    int Circle = 0;
    
    
    PsProcessSaveFile SaveFile("PsMux.mpeg");

    //unsigned char pTest[] = {0x00, 0x00, 0x00, 0x01, 0x27, 0x55, 0x66,
    //    0x00, 0x00, 0x00, 0x01, 0x28, 0x55, 
    //    0x00, 0x00, 0x00, 0x01, 0x25, 0x66};

    //SaveFile.testMuxSpsPpsI(pTest, sizeof(pTest));

    paac_encoder = new FaacEncoder();
    pcm_file = fopen("audio.pcm", "rb");

    FILE* fp = fopen('video.264', "rb");
    
    guint8* fReadbuf = new guint8[BUF_LEN];

    while(1)
    {
        int fReadsz = fread(fReadbuf, 1, BUF_LEN, fp);

        if(fReadsz <= 0){

            if (Circle){
                fseek(fp, 0, SEEK_SET);
                continue;
            }
            else{
                break;
            }
        }

        process_block(fReadbuf, fReadsz, 0xffff, &SaveFile);
    }

    delete []fReadbuf;

	return 0;
}

