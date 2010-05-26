// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license and patent
// grant that can be found in the LICENSE file in the root of the source
// tree. All contributing project authors may be found in the AUTHORS
// file in the root of the source tree.

#pragma once
#include <strmif.h>
#include <string>
#include "vp8decoderinpin.hpp"
#include "vp8decoderoutpin.hpp"
#include "vp8decoderidl.h"
#include "clockable.hpp"

namespace VP8DecoderLib
{

class Filter : public IBaseFilter,
               public IVP8PostProcessing,
               public CLockable
{
    friend HRESULT CreateInstance(
            IClassFactory*,
            IUnknown*, 
            const IID&, 
            void**);
    
    Filter(IClassFactory*, IUnknown*);
    virtual ~Filter();
    
    Filter(const Filter&);
    Filter& operator=(const Filter&);
    
public:

    //IUnknown

    HRESULT STDMETHODCALLTYPE QueryInterface(const IID&, void**);
    ULONG STDMETHODCALLTYPE AddRef();
    ULONG STDMETHODCALLTYPE Release();
    
    //IBaseFilter

    HRESULT STDMETHODCALLTYPE GetClassID(CLSID*);    
    HRESULT STDMETHODCALLTYPE Stop();    
    HRESULT STDMETHODCALLTYPE Pause();
    HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME);    
    HRESULT STDMETHODCALLTYPE GetState(DWORD, FILTER_STATE*);    
    HRESULT STDMETHODCALLTYPE SetSyncSource(IReferenceClock*);
    HRESULT STDMETHODCALLTYPE GetSyncSource(IReferenceClock**);
    HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins**);    
    HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR, IPin**);    
    HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO*);    
    HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph*, LPCWSTR);    
    HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR*);

    //IVP8PostProcessing
    
    HRESULT STDMETHODCALLTYPE SetFlags(int);    
    HRESULT STDMETHODCALLTYPE SetDeblockingLevel(int);    
    HRESULT STDMETHODCALLTYPE SetNoiseLevel(int);
    HRESULT STDMETHODCALLTYPE GetFlags(int*);    
    HRESULT STDMETHODCALLTYPE GetDeblockingLevel(int*);
    HRESULT STDMETHODCALLTYPE GetNoiseLevel(int*);
    HRESULT STDMETHODCALLTYPE ApplyPostProcessing();

    //local classes and methods

private:
    class CNondelegating : public IUnknown
    {
        CNondelegating(const CNondelegating&);
        CNondelegating& operator=(const CNondelegating&);

    public:
    
        Filter* const m_pFilter;
        LONG m_cRef;
        
        explicit CNondelegating(Filter*);
        virtual ~CNondelegating();
        
        HRESULT STDMETHODCALLTYPE QueryInterface(const IID&, void**);
        ULONG STDMETHODCALLTYPE AddRef();
        ULONG STDMETHODCALLTYPE Release();

    };
    
    IClassFactory* const m_pClassFactory;
    CNondelegating m_nondelegating;
    IUnknown* const m_pOuter;  //decl must follow m_nondelegating
    REFERENCE_TIME m_start;
    IReferenceClock* m_clock;
    
public:
    FILTER_INFO m_info;
    FILTER_STATE m_state;
    Inpin m_inpin;
    Outpin m_outpin;

    struct Config
    {
        int flags;
        int deblock;
        int noise;
    };
    
    Config m_cfg;
    
private:
    void OnStop();
    void OnStart();

};


}  //end namespace VP8DecoderLib
