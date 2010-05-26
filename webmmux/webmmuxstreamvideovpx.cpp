// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license and patent
// grant that can be found in the LICENSE file in the root of the source
// tree. All contributing project authors may be found in the AUTHORS
// file in the root of the source tree.

#include <strmif.h>
#include "webmmuxcontext.hpp"
#include "webmmuxstreamvideovpx.hpp"
#include <climits>
#include <cassert>
#ifdef _DEBUG
#include "odbgstream.hpp"
using std::endl;
using std::boolalpha;
#endif

namespace WebmMux
{

StreamVideoVPx::VPxFrame::VPxFrame(
    IMediaSample* pSample,
    StreamVideoVPx* pStream) :
    m_pSample(pSample)
{
    assert(m_pSample);
    m_pSample->AddRef();

    __int64 st, sp;  //reftime units

    const HRESULT hr = m_pSample->GetTime(&st, &sp);
    assert(SUCCEEDED(hr));    
    assert(st >= 0);    
    
    const __int64 ns = st * 100;  //nanoseconds
    
    const Context& ctx = pStream->m_context;
    const ULONG scale = ctx.GetTimecodeScale();
    assert(scale >= 1);
    
    const __int64 tc = ns / scale;
    assert(tc <= ULONG_MAX);
    
    m_timecode = static_cast<ULONG>(tc);
}


StreamVideoVPx::VPxFrame::~VPxFrame()
{
    const ULONG n = m_pSample->Release();
    n;
}


ULONG StreamVideoVPx::VPxFrame::GetTimecode() const
{    
    return m_timecode;
}


bool StreamVideoVPx::VPxFrame::IsKey() const
{
    return (m_pSample->IsSyncPoint() == S_OK);
}


ULONG StreamVideoVPx::VPxFrame::GetSize() const
{
    const long result = m_pSample->GetActualDataLength();
    assert(result >= 0);
    
    return result;
}


const BYTE* StreamVideoVPx::VPxFrame::GetData() const
{
    BYTE* ptr;
    
    const HRESULT hr = m_pSample->GetPointer(&ptr);
    assert(SUCCEEDED(hr));
    assert(ptr);
    
    return ptr;
}



StreamVideoVPx::StreamVideoVPx(
    Context& c,
    const AM_MEDIA_TYPE& mt) :
    StreamVideo(c, mt)
{
}


//void StreamVideoVPx::WriteTrackName()
//{
//    EbmlIO::File& f = m_context.m_file;
//
//    f.WriteID2(0x536E);   //name
//    f.Write1UTF8(L"VP8 video stream");  //TODO
//}



void StreamVideoVPx::WriteTrackCodecID()
{
    EbmlIO::File& f = m_context.m_file;

    f.WriteID1(0x86);  //Codec ID
    f.Write1String("V_VP8");
}


void StreamVideoVPx::WriteTrackCodecName()
{
    EbmlIO::File& f = m_context.m_file;

    f.WriteID3(0x258688);  //Codec Name
    f.Write1UTF8(L"VP8");
}


void StreamVideoVPx::WriteTrackSettings()
{
    EbmlIO::File& f = m_context.m_file;

    f.WriteID1(0xE0);  //video settings
    
    //allocate 2 bytes of storage for size of settings
    const __int64 begin_pos = f.SetPosition(2, STREAM_SEEK_CUR);    
    
    const BITMAPINFOHEADER& bmih = GetBitmapInfoHeader();
    assert(bmih.biSize >= sizeof(BITMAPINFOHEADER));
    assert(bmih.biWidth > 0);
    assert(bmih.biWidth <= USHRT_MAX);
    assert(bmih.biHeight > 0);
    assert(bmih.biHeight <= USHRT_MAX);
    
    const USHORT width = static_cast<USHORT>(bmih.biWidth);
    const USHORT height = static_cast<USHORT>(bmih.biHeight);

    f.WriteID1(0xB0);  //width
    f.Write1UInt(2);
    f.Serialize2UInt(width);
    
    f.WriteID1(0xBA);  //height
    f.Write1UInt(2);
    f.Serialize2UInt(height);
    
    const float framerate = GetFramerate();
    
    if (framerate > 0)
    {
        f.WriteID3(0x2383E3);  //frame rate
        f.Write1UInt(4);
        f.Serialize4Float(framerate);    
    }

    const __int64 end_pos = f.GetPosition();
    
    const __int64 size_ = end_pos - begin_pos;
    assert(size_ <= USHRT_MAX);
    
    const USHORT size = static_cast<USHORT>(size_);

    f.SetPosition(begin_pos - 2);
    f.Write2UInt(size);
    
    f.SetPosition(end_pos);
}


HRESULT StreamVideoVPx::Receive(IMediaSample* pSample)
{
    assert(pSample);

    VPxFrame* const pFrame = new (std::nothrow) VPxFrame(pSample, this);
    assert(pFrame);  //TODO
    
    assert(!m_vframes.empty() || pFrame->IsKey());
           
    m_vframes.push_back(pFrame);
    
#if 0
    odbgstream os;
    os << "webmmux::vpx::receive: time[ms]=" << pFrame->curr_timecode_ms()
       << " key=" << boolalpha << pFrame->IsKey()
       << " vframes.size=" << m_vframes.size()
       << " rframes.size=" << m_rframes.size()
       << endl;
#endif
   
    m_context.NotifyVideoFrame(this, pFrame);

    return S_OK;
}


int StreamVideoVPx::EndOfStream()
{
    return m_context.NotifyVideoEOS(this);
}


LONG StreamVideoVPx::GetLastTimecode() const
{
    if (m_vframes.empty())
        return -1;
        
    VideoFrame* const pFrame = m_vframes.back();
    assert(pFrame);
    
    return pFrame->GetTimecode();
}


}  //end namespace WebmMux

