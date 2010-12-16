// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#include <windows.h>
#include <windowsx.h>
#include <comdef.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <shlwapi.h>

#include <cassert>
#include <string>
#include <vector>

#include "debugutil.hpp"
#include "eventutil.hpp"
#include "hrtext.hpp"
#include "comreg.hpp"
#include "comdllwrapper.hpp"
#include "mfobjwrapper.hpp"
#include "webmtypes.hpp"

namespace WebmMfUtil
{

MfObjWrapperBase::MfObjWrapperBase()
{
}

MfObjWrapperBase::~MfObjWrapperBase()
{
}

MfByteStreamHandlerWrapper::MfByteStreamHandlerWrapper():
  audio_stream_count_(0),
  expected_media_event_type_(0),
  media_event_error_(S_OK),
  ref_count_(0),
  selected_stream_count_(0),
  stream_count_(0),
  video_stream_count_(0)
{
}

MfByteStreamHandlerWrapper::~MfByteStreamHandlerWrapper()
{
    if (ptr_media_src_)
    {
        ptr_media_src_->Shutdown();
        ptr_media_src_ = 0;
    }
    if (ptr_byte_stream_)
    {
        ptr_byte_stream_->Close();
        ptr_byte_stream_ = 0;
    }
    if (ptr_handler_)
    {
        ptr_handler_ = 0;
    }
}

HRESULT MfByteStreamHandlerWrapper::Create(std::wstring dll_path,
                                           GUID mfobj_clsid)
{
    HRESULT hr = ComDllWrapper::Create(dll_path, mfobj_clsid, &ptr_com_dll_);
    if (FAILED(hr) || !ptr_com_dll_)
    {
        DBGLOG("ComDllWrapper::Create failed path=" << dll_path.c_str()
            << HRLOG(hr));
        return hr;
    }
    hr = ptr_com_dll_->CreateInstance(IID_IMFByteStreamHandler,
        reinterpret_cast<void**>(&ptr_handler_));
    if (FAILED(hr) || !ptr_handler_)
    {
        DBGLOG("GetInterfacePtr failed" << HRLOG(hr));
        return hr;
    }
    hr = open_event_.Create();
    if (FAILED(hr))
    {
        DBGLOG("open event creation failed" << HRLOG(hr));
        return hr;
    }
    hr = media_source_event_.Create();
    if (FAILED(hr))
    {
        DBGLOG("media source event creation failed" << HRLOG(hr));
        return hr;
    }
    return hr;
}

HRESULT MfByteStreamHandlerWrapper::Create(
    std::wstring dll_path,
    GUID mfobj_clsid,
    MfByteStreamHandlerWrapper** ptr_bsh_instance)
{
    *ptr_bsh_instance = new (std::nothrow) MfByteStreamHandlerWrapper();

    if (!*ptr_bsh_instance)
    {
        DBGLOG("ctor failed");
        return E_OUTOFMEMORY;
    }

    MfByteStreamHandlerWrapper* ptr_bsh_wrapper = *ptr_bsh_instance;

    HRESULT hr = ptr_bsh_wrapper->Create(dll_path, mfobj_clsid);

    if (SUCCEEDED(hr))
    {
        ptr_bsh_wrapper->AddRef();
    }
    else
    {
        DBGLOG("ERROR, inner Create failed" << HRLOG(hr));
    }

    return hr;
}


HRESULT MfByteStreamHandlerWrapper::OpenURL(std::wstring url)
{
    if (1 > url.length())
    {
        DBGLOG("ERROR, empty url string");
        return E_INVALIDARG;
    }
    if (!ptr_handler_)
    {
        DBGLOG("ERROR, byte stream handler NULL");
        return E_OUTOFMEMORY;
    }
    HRESULT hr = MFCreateFile(MF_ACCESSMODE_READ,
                              MF_OPENMODE_FAIL_IF_NOT_EXIST,
                              MF_FILEFLAGS_NONE, url.c_str(),
                              &ptr_byte_stream_);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, MFCreateFile failed" << HRLOG(hr));
        return hr;
    }
    hr = ptr_handler_->BeginCreateObject(ptr_byte_stream_, url.c_str(), 0,
                                         NULL, NULL, this, NULL);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, byte stream handler BeginCreateObject failed, hr="
            << hr);
        return hr;
    }
    hr = open_event_.Wait();
    if (FAILED(hr))
    {
        DBGLOG("ERROR, open event wait failed" << HRLOG(hr));
        return hr;
    }
    if (!ptr_media_src_)
    {
        DBGLOG("ERROR, NULL media source");
        hr = E_OUTOFMEMORY;
    }
    return hr;
}

HRESULT MfByteStreamHandlerWrapper::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] =
    {
        QITABENT(MfByteStreamHandlerWrapper, IMFAsyncCallback),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

ULONG MfByteStreamHandlerWrapper::AddRef()
{
    return InterlockedIncrement(&ref_count_);
}

ULONG MfByteStreamHandlerWrapper::Release()
{
    UINT ref_count = InterlockedDecrement(&ref_count_);
    if (ref_count == 0)
    {
        delete this;
    }
    return ref_count;
}

// IMFAsyncCallback method
STDMETHODIMP MfByteStreamHandlerWrapper::GetParameters(DWORD*, DWORD*)
{
    // Implementation of this method is optional.
    return E_NOTIMPL;
}

// IMFAsyncCallback method
STDMETHODIMP MfByteStreamHandlerWrapper::Invoke(IMFAsyncResult* pAsyncResult)
{
    if (!ptr_media_src_ && !ptr_event_queue_)
    {
        assert(ptr_media_src_ == NULL);

        IUnknownPtr ptr_unk_media_src_;
        MF_OBJECT_TYPE obj_type;
        HRESULT hr = ptr_handler_->EndCreateObject(pAsyncResult, &obj_type,
                                                   &ptr_unk_media_src_);
        if (FAILED(hr) || !ptr_unk_media_src_)
        {
            DBGLOG("ERROR, EndCreateObject failed" << HRLOG(hr)
              << " return E_FAIL.");
            return E_FAIL;
        }
        else
        {
            ptr_media_src_ = ptr_unk_media_src_;
            hr = ptr_media_src_->QueryInterface(IID_IMFMediaEventGenerator,
                reinterpret_cast<void**>(&ptr_event_queue_));
            if (FAILED(hr) || !ptr_event_queue_)
            {
                DBGLOG("ERROR, failed to obtain media source event generator"
                  << HRLOG(hr) << " return E_FAIL.");
                return E_FAIL;
            }
        }
        return open_event_.Set();
    }
    else
    {
        IMFMediaEventPtr ptr_event;
        HRESULT hr = ptr_event_queue_->EndGetEvent(pAsyncResult, &ptr_event);
        if (FAILED(hr))
        {
            DBGLOG("ERROR, EndGetEvent failed" << HRLOG(hr)
              << " return E_FAIL.");
            return E_FAIL;
        }
        return HandleEvent_(ptr_event);
    }
}

HRESULT MfByteStreamHandlerWrapper::HandleEvent_(IMFMediaEventPtr& ptr_event)
{
    if (!ptr_event)
    {
        DBGLOG("ERROR, null event, return E_INVALIDARG");
        return E_INVALIDARG;
    }
    MediaEventType event_type;
    HRESULT hr = ptr_event->GetType(&event_type);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, cannot get event type" << HRLOG(hr)
          << " return E_FAIL.");
        return E_FAIL;
    }
    if (event_type != expected_media_event_type_)
    {
        DBGLOG("ERROR, unexpected event type, expected "
          << expected_media_event_type_ << " got " << event_type);
        return E_FAIL;
    }
    if (event_type == MENewStream)
    {
        hr = OnNewStream_(ptr_event);
    }
    return hr;
}

HRESULT MfByteStreamHandlerWrapper::OnNewStream_(IMFMediaEventPtr &ptr_event)
{
    PROPVARIANT event_val;
    PropVariantInit(&event_val);
    HRESULT hr = ptr_event->GetValue(&event_val);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, could not get event value" << HRLOG(hr));
        return hr;
    }
    // just assign the IUnknown val, what could go wrong... (shudder)
    IMFMediaStreamPtr ptr_media_stream = event_val.punkVal;
    if (!ptr_media_stream)
    {
        DBGLOG("ERROR, stream pointer null");
        return E_POINTER;
    }
    _COM_SMARTPTR_TYPEDEF(IMFStreamDescriptor, IID_IMFStreamDescriptor);
    IMFStreamDescriptorPtr ptr_stream_desc;
    hr = ptr_media_stream->GetStreamDescriptor(&ptr_stream_desc);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, could not get stream descriptor" << HRLOG(hr));
        return hr;
    }
    if (!ptr_stream_desc)
    {
        DBGLOG("ERROR, stream descriptor null");
        return E_POINTER;
    }
    _COM_SMARTPTR_TYPEDEF(IMFMediaTypeHandler, IID_IMFMediaTypeHandler);
    IMFMediaTypeHandlerPtr ptr_handler;
    hr = ptr_stream_desc->GetMediaTypeHandler(&ptr_handler);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, could not get media type handler" << HRLOG(hr));
        return hr;
    }
    if (!ptr_handler)
    {
        DBGLOG("ERROR, media type handler null");
        return E_POINTER;
    }
    GUID major_type = GUID_NULL;
    hr = ptr_handler->GetMajorType(&major_type);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, could not get major type" << HRLOG(hr));
        return hr;
    }
    if (MFMediaType_Audio != major_type && MFMediaType_Video != major_type)
    {
        DBGLOG("ERROR, unexpected major type (not audio or video)");
        return E_FAIL;
    }
    if (MFMediaType_Audio == major_type)
    {
        ptr_audio_stream_ = ptr_media_stream;
    }
    else
    {
        ptr_video_stream_ = ptr_media_stream;
    }
    return hr;
}

HRESULT MfByteStreamHandlerWrapper::OnSourceStarted_(
    IMFMediaEventPtr&)
{
    return E_NOTIMPL;
}

HRESULT MfByteStreamHandlerWrapper::WaitForMediaEvent_(DWORD expected_event)
{
    expected_media_event_type_ = expected_event;
    HRESULT hr = ptr_media_src_->BeginGetEvent(this, NULL);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, BeginGetEvent failed" << HRLOG(hr));
        return hr;
    }
    hr = media_source_event_.Wait();
    if (FAILED(hr))
    {
        DBGLOG("ERROR, media source event wait failed" << HRLOG(hr));
        return hr;
    }
    if (FAILED(expected_media_event_type_))
    {
        // when event handling fails the last error is stored in
        // |expected_media_event_type_|, just return it to the caller
        DBGLOG("ERROR, media source creation failed"
            << HRLOG(expected_media_event_type_));
        return expected_media_event_type_;
    }
    return hr;
}

HRESULT MfByteStreamHandlerWrapper::LoadMediaStreams()
{
    HRESULT hr = ptr_media_src_->CreatePresentationDescriptor(&ptr_pres_desc_);
    if (FAILED(hr) || !ptr_pres_desc_)
    {
        DBGLOG("ERROR, CreatePresentationDescriptor failed" << HRLOG(hr));
        return hr;
    }
    hr = ptr_pres_desc_->GetStreamDescriptorCount(&stream_count_);
    if (FAILED(hr) || !ptr_pres_desc_)
    {
        DBGLOG("ERROR, GetStreamDescriptorCount failed" << HRLOG(hr));
        return hr;
    }
    // get stream descriptors and store audio/video descs in our vectors
    for (DWORD i = 0; i < stream_count_; ++i)
    {
        BOOL selected;
        _COM_SMARTPTR_TYPEDEF(IMFStreamDescriptor, IID_IMFStreamDescriptor);
        IMFStreamDescriptorPtr ptr_desc;
        hr = ptr_pres_desc_->GetStreamDescriptorByIndex(i, &selected,
                                                        &ptr_desc);
        if (FAILED(hr) || !ptr_desc)
        {
            DBGLOG("ERROR, GetStreamDescriptorByIndex failed, count="
                << stream_count_ << " index=" << i << HRLOG(hr));
            return hr;
        }
        // TODO(tomfinegan): decide what to do w/unselected streams
        if (selected)
        {
            IMFMediaTypeHandlerPtr ptr_media_type_handler;
            hr = ptr_desc->GetMediaTypeHandler(&ptr_media_type_handler);
            if (FAILED(hr) || !ptr_media_type_handler)
            {
                DBGLOG("ERROR, GetMediaTypeHandler failed, count="
                    << stream_count_ << " index=" << i << HRLOG(hr));
                return hr;
            }
            GUID major_type;
            hr = ptr_media_type_handler->GetMajorType(&major_type);
            if (FAILED(hr))
            {
                DBGLOG("ERROR, GetMajorType failed, count=" << stream_count_
                    << " index=" << i << HRLOG(hr));
                return hr;
            }
            if (MFMediaType_Audio == major_type)
            {
                ++audio_stream_count_;
            }
            else if (MFMediaType_Video == major_type)
            {
                ++video_stream_count_;
            }
            ++selected_stream_count_;
        }
    }
    return hr;
}

HRESULT MfByteStreamHandlerWrapper::Start(LONGLONG start_time)
{
    if (!ptr_media_src_)
    {
        DBGLOG("ERROR, no media source");
        return E_INVALIDARG;
    }
    if (!ptr_pres_desc_)
    {
        DBGLOG("ERROR, no presentation descriptor");
        return E_INVALIDARG;
    }
    if (!audio_stream_count_ && !video_stream_count_)
    {
        DBGLOG("ERROR, no streams");
        return E_INVALIDARG;
    }
    if (!ptr_event_queue_)
    {
        DBGLOG("ERROR, no event queue");
        return E_INVALIDARG;
    }
    // store the time in a PROPVARIANT
    PROPVARIANT time_var;
    PropVariantInit(&time_var);
    time_var.vt = VT_I8;
    time_var.hVal.QuadPart = start_time;
    // GUID_NULL == TIME_FORMAT_NONE, 100 ns units (aka TIME_FORMAT_MEDIA_TIME
    // in DirectShow)
    const GUID time_format = GUID_NULL;
    HRESULT hr = ptr_media_src_->Start(ptr_pres_desc_, &time_format,
                                       &time_var);
    if (FAILED(hr))
    {
        DBGLOG("ERROR, IMFMediaSource::Start failed" << HRLOG(hr));
        return hr;
    }
    if (!ptr_audio_stream_ && !ptr_video_stream_)
    {
        UINT num_new_stream_events = audio_stream_count_ + video_stream_count_;
        UINT num_events = 0;
        // first start call, get the IMFMediaStream pointer(s)
        while (num_events < num_new_stream_events)
        {
            hr = WaitForMediaEvent_(MENewStream);
            if (FAILED(hr))
            {
                DBGLOG("ERROR, WaitForMediaEvent_ failed" << HRLOG(hr));
                return hr;
            }
            ++num_new_stream_events;
        }
    }
    return hr;
}

MfTransformWrapper::MfTransformWrapper()
{
}

MfTransformWrapper::~MfTransformWrapper()
{
}

HRESULT MfTransformWrapper::Create(std::wstring dll_path, GUID mfobj_clsid)
{
    HRESULT hr = ComDllWrapper::Create(dll_path, mfobj_clsid, &ptr_com_dll_);
    if (FAILED(hr) || !ptr_com_dll_)
    {
        DBGLOG("ComDllWrapper::Create failed path=" << dll_path.c_str()
            << HRLOG(hr));
        return hr;
    }
    hr = ptr_com_dll_->CreateInstance(IID_IMFTransform,
                                reinterpret_cast<void**>(&ptr_transform_));
    if (FAILED(hr) || !ptr_transform_)
    {
        DBGLOG("GetInterfacePtr failed" << HRLOG(hr));
        return hr;
    }
    return hr;
}

} // WebmMfUtil namespace
