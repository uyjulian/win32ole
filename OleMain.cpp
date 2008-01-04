#include <windows.h>
#include <DispEx.h>
#include "tp_stub.h"
#include <stdio.h>

// ATL
#if _MSC_VER == 1200
// Microsoft SDK �̂��̂Ƃ��������̂Ŕr��
#define __IHTMLControlElement_INTERFACE_DEFINED__
#endif

#include <atlbase.h>
static CComModule _Module;
#include <atlwin.h>
#include <atlcom.h>
#include <atliface.h>
#define _ATL_DLL
#include <atlhost.h>
#include <ExDispID.h>


static void log(const tjs_char *format, ...)
{
	va_list args;
	va_start(args, format);
	tjs_char msg[1024];
	_vsnwprintf(msg, 1024, format, args);
	TVPAddLog(msg);
	va_end(args);
}

//---------------------------------------------------------------------------

#include "IDispatchWrapper.hpp"

/**
 * OLE -> �g���g�� �C�x���g�f�B�X�p�b�`��
 * sender (IUnknown) ���� DIID �̃C�x���g���󗝂��A
 * receiver (tTJSDispatch2) �ɑ��M����B
 */ 
class EventSink : public IDispatch
{
protected:
	int refCount;
	REFIID diid;
	ITypeInfo *pTypeInfo;
	iTJSDispatch2 *receiver;

public:
	EventSink(GUID diid, ITypeInfo *pTypeInfo, iTJSDispatch2 *receiver) : diid(diid), pTypeInfo(pTypeInfo), receiver(receiver) {
		refCount = 1;
		if (pTypeInfo) {
			pTypeInfo->AddRef();
		}
		if (receiver) {
			receiver->AddRef();
		}
	}

	~EventSink() {
		if (receiver) {
			receiver->Release();
		}
		if (pTypeInfo) {
			pTypeInfo->Release();
		}
	}

	//----------------------------------------------------------------------------
	// IUnknown ����
	
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
											 void __RPC_FAR *__RPC_FAR *ppvObject) {
		if (riid == IID_IUnknown ||
			riid == IID_IDispatch||
			riid == diid) {
			if (ppvObject == NULL)
				return E_POINTER;
			*ppvObject = this;
			AddRef();
			return S_OK;
		} else {
			*ppvObject = 0;
			return E_NOINTERFACE;
		}
	}

	ULONG STDMETHODCALLTYPE AddRef() {
		refCount++;
		return refCount;
	}

	ULONG STDMETHODCALLTYPE Release() {
		int ret = --refCount;
		if (ret <= 0) {
			delete this;
			ret = 0;
		}
		return ret;
	}
	
	// -------------------------------------
	// IDispatch �̎���
public:
	STDMETHOD (GetTypeInfoCount) (UINT* pctinfo)
	{
		return	E_NOTIMPL;
	}

	STDMETHOD (GetTypeInfo) (UINT itinfo, LCID lcid, ITypeInfo** pptinfo)
	{
		return	E_NOTIMPL;
	}

	STDMETHOD (GetIDsOfNames) (REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgdispid)
	{
		return	E_NOTIMPL;
	}

	STDMETHOD (Invoke) (DISPID dispid, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pdispparams, VARIANT* pvarResult, EXCEPINFO* pexcepinfo, UINT* puArgErr)
	{
		BSTR bstr = NULL;
		if (pTypeInfo) {
			unsigned int len;
			pTypeInfo->GetNames(dispid, &bstr, 1, &len);
		}
		HRESULT hr = IDispatchWrapper::InvokeEx(receiver, bstr, wFlags, pdispparams, pvarResult, pexcepinfo);
		if (hr == DISP_E_MEMBERNOTFOUND) {
			//log(L"member not found:%ws", bstr);
			hr = S_OK;
		}
		if (bstr) {
			SysFreeString(bstr);
		}
		return hr;
	}
};

//---------------------------------------------------------------------------

/*
 * WIN32OLE �l�C�e�B�u�C���X�^���X
 */
class NI_WIN32OLE : public tTJSNativeInstance // �l�C�e�B�u�C���X�^���X
{
public:
	IDispatch *pDispatch;

protected:
	struct EventInfo {
		IID diid;
		DWORD cookie;
		EventInfo(REFIID diid, DWORD cookie) : diid(diid), cookie(cookie) {};
	};
	vector<EventInfo> events;

	void clearEvent() {
		if (pDispatch) {
			vector<EventInfo>::iterator i = events.begin();
			while (i != events.end()) {
				AtlUnadvise(pDispatch, i->diid, i->cookie);
				i++;
			}
			events.clear();
		}
	}

public:
	NI_WIN32OLE() {
		// �R���X�g���N�^
		pDispatch = NULL;
	}

	tjs_error TJS_INTF_METHOD
		Construct(tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *tjs_obj)
	{
		// TJS2 �I�u�W�F�N�g���쐬�����Ƃ��ɌĂ΂��
		if (numparams >= 1) {
			if (param[0]->Type() == tvtString) {
				const tjs_char *name = param[0]->GetString();
				HRESULT hr;
				CLSID   clsid;
				OLECHAR *oleName = SysAllocString(name);
				if (FAILED(hr = CLSIDFromProgID(oleName, &clsid))) {
					hr = CLSIDFromString(oleName, &clsid);
				}
				SysFreeString(oleName);
				if (SUCCEEDED(hr)) {
					// COM �ڑ�����IDispatch ���擾����
					/* get IDispatch interface */
					hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&pDispatch);
					if (SUCCEEDED(hr)) {
						// missing ���\�b�h�̓o�^
						tTJSVariant name(TJS_W("missing"));
						tjs_obj->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &name);
						return TJS_S_OK;
					} else {
						log(L"CoCreateInstance failed %ws", name);
					}
				} else {
					log(L"bad CLSID %ws", name);
				}
			} 
			return TJS_E_INVALIDPARAM;
		} else {
			return TJS_E_BADPARAMCOUNT;
		}
	}

	void TJS_INTF_METHOD Invalidate()
	{
		// �I�u�W�F�N�g�������������Ƃ��ɌĂ΂��
		clearEvent();
		if (pDispatch) {
			pDispatch->Release();
		}
	}

	/**
	 * ���\�b�h���s
	 */
	tjs_error invoke(DWORD wFlags,
					 const tjs_char *membername,
					 tTJSVariant *result,
					 tjs_int numparams,
					 tTJSVariant **param) {
		if (pDispatch) {
			return iTJSDispatch2Wrapper::Invoke(pDispatch,
												wFlags,
												membername,
												result,
												numparams,
												param);
		}
		return TJS_E_FAIL;
	}
	
	/**
	 * ���\�b�h���s
	 */
	tjs_error invoke(DWORD wFlags,
					 tTJSVariant *result,
					 tjs_int numparams,
					 tTJSVariant **param) {
		//log(L"native invoke %d", numparams);
		if (pDispatch) {
			// �p�����[�^�̂P�ڂ����\�b�h��
			if (numparams > 0) {
				if (param[0]->Type() == tvtString) {
					return iTJSDispatch2Wrapper::Invoke(pDispatch,
														wFlags,
														param[0]->GetString(),
														result,
														numparams - 1,
														param ? param + 1 : NULL);
				} else {
					return TJS_E_INVALIDPARAM;
				}
			} else {
				return TJS_E_BADPARAMCOUNT;
			}
		}
		return TJS_E_FAIL;
	}

	/**
	 * ���\�b�h���s
	 */
	tjs_error missing(tTJSVariant *result, tjs_int numparams, tTJSVariant **param) {

		if (numparams < 3) {return TJS_E_BADPARAMCOUNT;};
		bool ret = false;
		const tjs_char *membername = param[1]->GetString();
		if ((int)*param[0]) {
			// put
			ret = TJS_SUCCEEDED(invoke(DISPATCH_PROPERTYPUT, membername, NULL, 1, &param[2]));
		} else {
			// get
			tTJSVariant result;
			tjs_error err;
			ret = TJS_SUCCEEDED(err = invoke(DISPATCH_PROPERTYGET|DISPATCH_METHOD, membername, &result, 0, NULL));
			if (err == TJS_E_BADPARAMCOUNT) {
				result = new iTJSDispatch2WrapperForMethod(pDispatch, membername);
				ret = true;
			}
			if (ret) {
				iTJSDispatch2 *value = param[2]->AsObject();
				if (value) {
					value->PropSet(0, NULL, NULL, &result, NULL);
					value->Release();
				}
			}
		}
		if (result) {
			*result = ret;
		}
		return TJS_S_OK;
	}


protected:

	/**
	 * �f�t�H���g�� IID ��T��
	 * @param pitf ���O
	 * @param piid �擾����IID�̊i�[��
	 * @param ppTypeInfo �֘A��������
	 */
	
	HRESULT findDefaultIID(IID *piid, ITypeInfo **ppTypeInfo) {

		HRESULT hr;

		IProvideClassInfo2 *pProvideClassInfo2;
		hr = pDispatch->QueryInterface(IID_IProvideClassInfo2, (void**)&pProvideClassInfo2);
		if (SUCCEEDED(hr)) {
			hr = pProvideClassInfo2->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, piid);
			pProvideClassInfo2->Release();
			ITypeInfo *pTypeInfo;
			if (SUCCEEDED(hr = pDispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &pTypeInfo))) {
				ITypeLib *pTypeLib;
				unsigned int index;
				if (SUCCEEDED(hr = pTypeInfo->GetContainingTypeLib(&pTypeLib, &index))) {
					hr = pTypeLib->GetTypeInfoOfGuid(*piid, ppTypeInfo);
				}
			}
			return hr;
		}

		IProvideClassInfo *pProvideClassInfo;
		if (SUCCEEDED(hr = pDispatch->QueryInterface(IID_IProvideClassInfo, (void**)&pProvideClassInfo))) {
			ITypeInfo *pTypeInfo;
			if (SUCCEEDED(hr = pProvideClassInfo->GetClassInfo(&pTypeInfo))) {
				
				TYPEATTR *pTypeAttr;
				if (SUCCEEDED(hr = pTypeInfo->GetTypeAttr(&pTypeAttr))) {
					int i;
					for (i = 0; i < pTypeAttr->cImplTypes; i++) {
						int iFlags;
						if (SUCCEEDED(hr = pTypeInfo->GetImplTypeFlags(i, &iFlags))) {
							if ((iFlags & IMPLTYPEFLAG_FDEFAULT) &&	(iFlags & IMPLTYPEFLAG_FSOURCE)) {
								HREFTYPE hRefType;
								if (SUCCEEDED(hr = pTypeInfo->GetRefTypeOfImplType(i, &hRefType))) {
									if (SUCCEEDED(hr = pTypeInfo->GetRefTypeInfo(hRefType, ppTypeInfo))) {
										break;
									}
								}
							}
						}
					}
					pTypeInfo->ReleaseTypeAttr(pTypeAttr);
				}
				pTypeInfo->Release();
			}
			pProvideClassInfo->Release();
		}

		if (!*ppTypeInfo) {
			if (SUCCEEDED(hr)) {
				hr = E_UNEXPECTED;
			}
		} else {
			TYPEATTR *pTypeAttr;
			hr = (*ppTypeInfo)->GetTypeAttr(&pTypeAttr);
			if (SUCCEEDED(hr)) {
				*piid = pTypeAttr->guid;
				(*ppTypeInfo)->ReleaseTypeAttr(pTypeAttr);
			} else {
				(*ppTypeInfo)->Release();
				*ppTypeInfo = NULL;
			}
		}
		return hr;
	}
	
	/**
	 * IID ��T��
	 * @param pitf ���O
	 * @param piid �擾����IID�̊i�[��
	 * @param ppTypeInfo �֘A��������
	 */
	HRESULT findIID(const tjs_char *pitf, IID *piid, ITypeInfo **ppTypeInfo) {

		if (pitf == NULL) {
			return findDefaultIID(piid, ppTypeInfo);
		}

		HRESULT hr;
		ITypeInfo *pTypeInfo;
		if (SUCCEEDED(hr = pDispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &pTypeInfo))) {
			ITypeLib *pTypeLib;
			unsigned int index;
			if (SUCCEEDED(hr = pTypeInfo->GetContainingTypeLib(&pTypeLib, &index))) {
				bool found = false;
				unsigned int count = pTypeLib->GetTypeInfoCount();
				for (index = 0; index < count; index++) {
					ITypeInfo *pTypeInfo;
					if (SUCCEEDED(pTypeLib->GetTypeInfo(index, &pTypeInfo))) {
						TYPEATTR *pTypeAttr;
						if (SUCCEEDED(pTypeInfo->GetTypeAttr(&pTypeAttr))) {
							if (pTypeAttr->typekind == TKIND_COCLASS) {
								int type;
								for (type = 0; !found && type < pTypeAttr->cImplTypes; type++) {
									HREFTYPE RefType;
									if (SUCCEEDED(pTypeInfo->GetRefTypeOfImplType(type, &RefType))) {
										ITypeInfo *pImplTypeInfo;
										if (SUCCEEDED(pTypeInfo->GetRefTypeInfo(RefType, &pImplTypeInfo))) {
											BSTR bstr = NULL;
											if (SUCCEEDED(pImplTypeInfo->GetDocumentation(-1, &bstr, NULL, NULL, NULL))) {
												if (wcscmp(pitf, bstr) == 0) {
													TYPEATTR *pImplTypeAttr;
													if (SUCCEEDED(pImplTypeInfo->GetTypeAttr(&pImplTypeAttr))) {
														found = true;
														*piid = pImplTypeAttr->guid;
														if (ppTypeInfo) {
															*ppTypeInfo = pImplTypeInfo;
															(*ppTypeInfo)->AddRef();
														}
														pImplTypeInfo->ReleaseTypeAttr(pImplTypeAttr);
													}
												}
												SysFreeString(bstr);
											}
											pImplTypeInfo->Release();
										}
									}
								}
							}
							pTypeInfo->ReleaseTypeAttr(pTypeAttr);
						}
						pTypeInfo->Release();
					}
					if (found) {
						break;
					}
				}
				if (!found) {
					hr = E_NOINTERFACE;
				}
				pTypeLib->Release();
			}
			pTypeInfo->Release();
		}
		return hr;
	}

	bool addEvent(const tjs_char *diidName, iTJSDispatch2 *receiver) {
		bool ret = false;
		IID diid;
		ITypeInfo *pTypeInfo;
		if (SUCCEEDED(findIID(diidName, &diid, &pTypeInfo))) {
			EventSink *sink = new EventSink(diid, pTypeInfo, receiver);
			DWORD cookie;
			if (SUCCEEDED(AtlAdvise(pDispatch, sink, diid, &cookie))) {
				events.push_back(EventInfo(diid, cookie));
				ret = true;
			}
			sink->Release();
			if (pTypeInfo) {
				pTypeInfo->Release();
			}
		}
		return ret;
	}
	
public:

	tjs_error addEvent(tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		const tjs_char *diidName = param[0]->GetString();
		bool success = false;
		if (numparams > 1) {
			iTJSDispatch2 *receiver = param[1]->AsObject();
			if (receiver) {
				success = addEvent(diidName, receiver);
				receiver->Release();
			}
		} else {
			success = addEvent(diidName, objthis);
		}
		if (!success) {
			log(L"�C�x���g[%ws]�̓o�^�Ɏ��s���܂���", diidName);
		}
		return TJS_S_OK;
	}
	
	/**
	 * �萔�̎擾
	 */
protected:

	/**
	 * �萔�̎擾
	 * @param pTypeInfo TYPEINFO
	 * @param target �i�[��
	 */
	void getConstant(ITypeInfo *pTypeInfo, iTJSDispatch2 *target) {
		// �����
		TYPEATTR  *pTypeAttr = NULL;
		if (SUCCEEDED(pTypeInfo->GetTypeAttr(&pTypeAttr))) {
			for (int i=0; i<pTypeAttr->cVars; i++) {
				VARDESC *pVarDesc = NULL;
				if (SUCCEEDED(pTypeInfo->GetVarDesc(i, &pVarDesc))) {
					if (pVarDesc->varkind == VAR_CONST &&
						!(pVarDesc->wVarFlags & (VARFLAG_FHIDDEN | VARFLAG_FRESTRICTED | VARFLAG_FNONBROWSABLE))) {
						BSTR bstr = NULL;
						unsigned int len;
						if (SUCCEEDED(pTypeInfo->GetNames(pVarDesc->memid, &bstr, 1, &len)) && len >= 0 && bstr) {
							//log(L"const:%s", bstr);
							tTJSVariant result;
							IDispatchWrapper::storeVariant(result, *(pVarDesc->lpvarValue));
							target->PropSet(TJS_MEMBERENSURE|TJS_IGNOREPROP,
											bstr,
											NULL,
											&result,
											target
											);
							SysFreeString(bstr);
						}
					}
					pTypeInfo->ReleaseVarDesc(pVarDesc);
				}
			}
			pTypeInfo->ReleaseTypeAttr(pTypeAttr);
		}
	}

	/**
	 * �萔�̎擾
	 * @param pTypeLib TYPELIB
	 * @param target �i�[��
	 */
	void getConstant(ITypeLib *pTypeLib, iTJSDispatch2 *target) {
		unsigned int count = pTypeLib->GetTypeInfoCount();
		for (unsigned int i=0; i<count; i++) {
			ITypeInfo *pTypeInfo = NULL;
			if (SUCCEEDED(pTypeLib->GetTypeInfo(i, &pTypeInfo))) {
				getConstant(pTypeInfo, target);
				pTypeInfo->Release();
			}
		}
	}

public:
	/**
	 * �萔�̎擾
	 * @param target �i�[��
	 */
	void getConstant(iTJSDispatch2 *target) {
		if (target) {
			ITypeInfo *pTypeInfo = NULL;
			if (SUCCEEDED(pDispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &pTypeInfo))) {
				unsigned int index = 0;
				ITypeLib *pTypeLib = NULL;
				if (SUCCEEDED(pTypeInfo->GetContainingTypeLib(&pTypeLib, &index))) {
					getConstant(pTypeLib, target);
					pTypeLib->Release();
				}
				pTypeInfo->Release();
			}
		}
	}

	/**
	 * ���\�b�h���s
	 */
	tjs_error getConstant(tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (numparams > 0) {
			iTJSDispatch2 *store = param[0]->AsObject();
			if (store) {
				getConstant(store);
				store->Release();
			}
		} else {
			getConstant(objthis);
		}
		return TJS_S_OK;
	}

};

//---------------------------------------------------------------------------
/*
	����� NI_WIN32OLE �̃I�u�W�F�N�g���쐬���ĕԂ������̊֐��ł��B
	��q�� TJSCreateNativeClassForPlugin �̈����Ƃ��ēn���܂��B
*/
static iTJSNativeInstance * TJS_INTF_METHOD Create_NI_WIN32OLE()
{
	return new NI_WIN32OLE();
}
//---------------------------------------------------------------------------
/*
	TJS2 �̃l�C�e�B�u�N���X�͈�ӂ� ID �ŋ�ʂ���Ă���K�v������܂��B
	����͌�q�� TJS_BEGIN_NATIVE_MEMBERS �}�N���Ŏ����I�Ɏ擾����܂����A
	���� ID ���i�[����ϐ����ƁA���̕ϐ��������Ő錾���܂��B
	�����l�ɂ͖����� ID ��\�� -1 ���w�肵�Ă��������B
*/
#define TJS_NATIVE_CLASSID_NAME ClassID_WIN32OLE
static tjs_int32 TJS_NATIVE_CLASSID_NAME = -1;

//---------------------------------------------------------------------------
/*
 *	TJS2 �p�́u�N���X�v���쐬���ĕԂ��֐��ł��B
*/
static iTJSDispatch2 * Create_NC_WIN32OLE()
{
	// �N���X�I�u�W�F�N�g
	tTJSNativeClassForPlugin * classobj = TJSCreateNativeClassForPlugin(TJS_W("WIN32OLE"), Create_NI_WIN32OLE);

	// �l�C�e�B�u�����o�[
	TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/WIN32OLE)

		TJS_DECL_EMPTY_FINALIZE_METHOD

		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(
			/*var.name*/_this,
			/*var.type*/NI_WIN32OLE,
			/*TJS class name*/WIN32OLE)
		{
			// NI_WIN32OLE::Construct �ɂ����e���L�q�ł���̂�
			// �����ł͉������Ȃ�
			return TJS_S_OK;
		}
		TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/WIN32OLE)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/invoke) // invoke ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->invoke(DISPATCH_PROPERTYGET|DISPATCH_METHOD, result, numparams, param);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/invoke)
			
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/set) // set ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->invoke(DISPATCH_PROPERTYPUT, result, numparams, param);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/set)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/get) // get ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->invoke(DISPATCH_PROPERTYGET, result, numparams, param);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/get)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/missing) // missing ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->missing(result, numparams, param);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/missing)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getConstant) // getConstant ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->getConstant(numparams, param, objthis);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/getConstant)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/addEvent) // addEvent ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->addEvent(numparams, param, objthis);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/addEvent)
			
	TJS_END_NATIVE_MEMBERS

	return classobj;
}
//---------------------------------------------------------------------------
/*
	TJS_NATIVE_CLASSID_NAME �͈ꉞ undef ���Ă������ق����悢�ł��傤
*/
#undef TJS_NATIVE_CLASSID_NAME
//---------------------------------------------------------------------------


/**
 * DHTML�̊O���ďo��(window.external)�Ɗe��UI�������󗝂��邽�߂̃C���^�[�t�F�[�X�N���X�B
 * ���ꂼ��N���X���̑Ή����郁�\�b�h���Ăяo���Ă���B�p�����Ē��g���L�q���邱�Ƃ�
 * �������ύX�ł���B�p�����[�^��COM�X�^�C���̂���(VARIANT)�ɂȂ�̂Œ���
 * XXX TJS �̃��\�b�h���Ăяo���ł���悤�Ɍ�ŉ��Ǘ\��B���݂̂��̂́u���������Ȃ��v
 * ���߂̂��̂ɂȂ��Ă���B
 */ 
class CExternalUI : public IDocHostUIHandlerDispatch {

protected:
	IDispatchEx *dispatchEx;
	
public:
	CExternalUI() {
		iTJSDispatch2 * global = TVPGetScriptDispatch();
		dispatchEx = new IDispatchWrapper(global);
		global->Release();
	}

	~CExternalUI() {
		dispatchEx->Release();
	}

	//----------------------------------------------------------------------------
	// IUnknown ����
	
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
											 void __RPC_FAR *__RPC_FAR *ppvObject) {
		if (dispatchEx && (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IDispatchEx)) {
			//log(L"get dispatchEx");
			if (ppvObject == NULL)
				return E_POINTER;
			dispatchEx->AddRef();
			*ppvObject = dispatchEx;
			return S_OK;
		} else if (riid == IID_IUnknown || riid == IID_IDispatch) {
			if (ppvObject == NULL)
				return E_POINTER;
			*ppvObject = this;
			AddRef();
			return S_OK;
		} else {
			*ppvObject = 0;
			return E_NOINTERFACE;
		}
	}

	// XXX MSHTML ����̌Ăяo�������������̂ŊJ�����Ȃ��悤�ɂ��Ă���
	ULONG STDMETHODCALLTYPE AddRef() {
		return 1;
	}

	// XXX MSHTML ����̌Ăяo�������������̂ŊJ�����Ȃ��悤�ɂ��Ă���
	ULONG STDMETHODCALLTYPE Release() {
		return 1;
	}

	//----------------------------------------------------------------------------
	// IDispatch ����

	STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) {
		return E_NOTIMPL;
	}

	STDMETHOD(GetTypeInfo)(UINT itinfo, LCID lcid, ITypeInfo** pptinfo) {
		return E_NOTIMPL;
	}

	/**
	 * ���\�b�h����ID�̑Ή����Ƃ郁�\�b�h
	 * regszNames �Ƀ��\�b�h���̔z�񂪂���̂ŁA
	 * rgdispid �ɑΉ����� dispid ��Ԃ��Ă��
	 */
	STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR* rgszNames, UINT cNames,
							 LCID lcid, DISPID* rgdispid) {
		return E_NOTIMPL;
	}

	/**
	 * ���\�b�h���s
	 * dispidMember �Ń��\�b�h���w�肳���B������ pdispparams �� VARIANT �̔z��
	 * �̌`�ł킽�����̂ł�����g��
	 */
	STDMETHOD(Invoke)(DISPID dispidMember, REFIID riid,
					  LCID lcid, WORD wFlags, DISPPARAMS* pdispparams, VARIANT* pvarResult,
					  EXCEPINFO* pexcepinfo, UINT* puArgErr) {
		return E_NOTIMPL;
	}

	//----------------------------------------------------------------------------
	// IDocHostUIHandlerDispatch �̎���

	
	/**
	 * �R���e�L�X�g���j���[����
	 * �������Ȃ����ƂŃ��j���[�������Ă���
	 */
	HRESULT STDMETHODCALLTYPE ShowContextMenu( 
		/* [in] */ DWORD dwID,
		/* [in] */ DWORD x,
		/* [in] */ DWORD y,
		/* [in] */ IUnknown __RPC_FAR *pcmdtReserved,
		/* [in] */ IDispatch __RPC_FAR *pdispReserved,
		/* [retval][out] */ HRESULT __RPC_FAR *dwRetVal) {
		*dwRetVal = S_OK;      //This is what the WebBrowser control is looking for.
		//You can show your own context menu here.
		return S_OK;        
	}

	HRESULT STDMETHODCALLTYPE GetHostInfo( 
		/* [out][in] */ DWORD __RPC_FAR *pdwFlags,
		/* [out][in] */ DWORD __RPC_FAR *pdwDoubleClick) {
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE ShowUI( 
		/* [in] */ DWORD dwID,
		/* [in] */ IUnknown __RPC_FAR *pActiveObject,
		/* [in] */ IUnknown __RPC_FAR *pCommandTarget,
		/* [in] */ IUnknown __RPC_FAR *pFrame,
		/* [in] */ IUnknown __RPC_FAR *pDoc,
		/* [retval][out] */ HRESULT __RPC_FAR *dwRetVal) {
		return E_NOTIMPL;
	}
        
	HRESULT STDMETHODCALLTYPE HideUI( void) {
		return E_NOTIMPL;
	}
        
	HRESULT STDMETHODCALLTYPE UpdateUI( void) {
		return E_NOTIMPL;
	}
        
	HRESULT STDMETHODCALLTYPE EnableModeless(
		/* [in] */ VARIANT_BOOL fEnable) {
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE OnDocWindowActivate( 
		/* [in] */ VARIANT_BOOL fActivate) {
		return E_NOTIMPL;
	}
        
	HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(
		/* [in] */ VARIANT_BOOL fActivate) {
		return E_NOTIMPL;
	}
        
	HRESULT STDMETHODCALLTYPE ResizeBorder( 
		/* [in] */ long left,
		/* [in] */ long top,
		/* [in] */ long right,
		/* [in] */ long bottom,
		/* [in] */ IUnknown __RPC_FAR *pUIWindow,
		/* [in] */ VARIANT_BOOL fFrameWindow) {
		return E_NOTIMPL;
	}
	
	HRESULT STDMETHODCALLTYPE TranslateAccelerator( 
		/* [in] */ DWORD hWnd,
		/* [in] */ DWORD nMessage,
		/* [in] */ DWORD wParam,
		/* [in] */ DWORD lParam,
		/* [in] */ BSTR bstrGuidCmdGroup,
		/* [in] */ DWORD nCmdID,
		/* [retval][out] */ HRESULT __RPC_FAR *dwRetVal) {
		return E_NOTIMPL;
	}
	
	HRESULT STDMETHODCALLTYPE GetOptionKeyPath( 
		/* [out] */ BSTR __RPC_FAR *pbstrKey,
		/* [in] */ DWORD dw) {
		return E_NOTIMPL;
	}
        
	HRESULT STDMETHODCALLTYPE GetDropTarget( 
		/* [in] */ IUnknown __RPC_FAR *pDropTarget,
		/* [out] */ IUnknown __RPC_FAR *__RPC_FAR *ppDropTarget) {
		return E_NOTIMPL;
	}
        
	HRESULT STDMETHODCALLTYPE GetExternal( 
		/* [out] */ IDispatch __RPC_FAR *__RPC_FAR *ppDispatch) {
		*ppDispatch = this;
		return S_OK;
	}
        
	HRESULT STDMETHODCALLTYPE TranslateUrl( 
		/* [in] */ DWORD dwTranslate,
		/* [in] */ BSTR bstrURLIn,
		/* [out] */ BSTR __RPC_FAR *pbstrURLOut) {
		return E_NOTIMPL;
	}
        
	HRESULT STDMETHODCALLTYPE FilterDataObject( 
		/* [in] */ IUnknown __RPC_FAR *pDO,
		/* [out] */ IUnknown __RPC_FAR *__RPC_FAR *ppDORet) {
		return E_NOTIMPL;
	}
};

/*
 * ActiveX �l�C�e�B�u�C���X�^���X
 */
class NI_ActiveX : public NI_WIN32OLE, CWindowImpl<NI_ActiveX, CAxWindow>
{
protected:
	int left;
	int top;
	int width;
	int height;

protected:
	CExternalUI *externalUI;

public:
	NI_ActiveX() {
		externalUI = NULL;
	}

	~NI_ActiveX() {
		if (externalUI) {
			delete externalUI;
		}
	}
	
public:
	/**
	 * �R���X�g���N�^
	 * @param name  ���ʖ�
	 * @param left  �\���ʒu
	 * @param top   �\���ʒu
	 * @param width  �\���T�C�Y
	 * @param height �����w��
	 */
	tjs_error TJS_INTF_METHOD
		Construct(tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *tjs_obj)
	{
		left = 0;
		top  = 0;
		width  = 100; // �f�t�H���g
		height = 100; // �f�t�H���g
		
		// �p�����[�^�͂ЂƂK�{
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		// ���p�����[�^�͕�����
		if (param[0]->Type() != tvtString) {
			return TJS_E_INVALIDPARAM;
		}
			
		const tjs_char *name = param[0]->GetString();
		OLECHAR *oleName = SysAllocString(name);

		// �O���p
		HWND handle = 0;
		DWORD style = WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN;

		if (numparams >= 2) {
			// ���p�����[�^���� HWND ���擾
			iTJSDispatch2 *win = param[1]->AsObjectNoAddRef();
			tTJSVariant hwnd;
			if (win->PropGet(0, TJS_W("HWND"), NULL, &hwnd, win) == TJS_S_OK) {
				handle = (HWND)(int)hwnd;
				style = WS_CHILD|WS_CLIPCHILDREN|WS_VISIBLE; // �q�E�C���h�E
				RECT rect;
				::GetClientRect(handle, &rect);   // �e�S�̈�
				left   = rect.left;
				top    = rect.top;
				width  = rect.right - rect.left;
				height = rect.bottom - rect.top;
			}
			// �\���̈�̕ύX
			if (numparams >= 6) {
				left   = (int)param[2];
				top    = (int)param[3];
				width  = (int)param[4];
				height = (int)param[5];
			}
		}

		RECT rect;
		rect.left   = left;
		rect.top    = top;
		rect.right  = left + width;
		rect.bottom = top  + height;
		
		tjs_error ret = TJS_E_INVALIDPARAM;
		HRESULT hr;

		// �E�C���h�E����
		// �E�C���h�E����
		if (Create(handle, rect, NULL, style)) {

			// �R���g���[������
			hr = CreateControl(oleName);
			if (SUCCEEDED(hr)) {
				// IDispatch�擾
				hr = QueryControl(IID_IDispatch, (void**)&pDispatch);
				if (SUCCEEDED(hr)) {
					// missing ���\�b�h�̓o�^
					tTJSVariant name(TJS_W("missing"));
					tjs_obj->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &name);
					ret = TJS_S_OK;
				} else {
					log(L"QueryControl failed %ws", name);
				}

				// external �� global ��o�^����
				{
					iTJSDispatch2 * global = TVPGetScriptDispatch();
					IDispatchEx *dispatchEx = new IDispatchWrapper(global);
					SetExternalDispatch(dispatchEx);
					dispatchEx->Release();
					global->Release();
				}
				
			} else {
				log(L"CreateControl failed %ws", name);
			}
			SysFreeString(oleName);
		}
		return ret;
	}

	void TJS_INTF_METHOD Invalidate()
	{
		// �I�u�W�F�N�g�������������Ƃ��ɌĂ΂��
		NI_WIN32OLE::Invalidate();
		if (m_hWnd) { DestroyWindow(); }
	}

	// -----------------------------------------------------------------------

	/**
	 * �O���g���n���h���̓o�^
	 * �������� IE ��p�̏���
	 */
	tjs_error setExternalUI(tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
		externalUI = new CExternalUI();
		SetExternalUIHandler(externalUI);
		return S_OK;
	}

	// -----------------------------------------------------------------------
	
	void setVisible(bool visible) {
		if (m_hWnd) {
			if (visible) {
				setPos();
			}
			ShowWindow(visible);
		}
	}

	bool getVisible() {
		return m_hWnd && IsWindowVisible();
	}

protected:
	void setPos() {
		if (m_hWnd) {
			SetWindowPos(0, left, top, width, height, 0);
		}
	}

public:
	void setLeft(int l) {
		left = l;
		setPos();
	}

	int getLeft() {
		return left;
	}

	void setTop(int t) {
		top = t;
		setPos();
	}

	int getTop() {
		return top;
	}
	
	void setWidth(int w) {
		width = w;
		setPos();
	}

	int getWidth() {
		return width;
	}

	void setHeight(int h) {
		height = h;
		setPos();
	}

	int getHeight() {
		return height;
	}
	
	/**
	 * ���ꏊ�w��
	 */	
	void setPos(int l, int t) {
		left = l;
		top  = t;
		setPos();
	}

	/**
	 * ���T�C�Y�w��
	 */	
	void setSize(int w, int h) {
		width = w;
		height = h;
		setPos();
	}

	BEGIN_MSG_MAP(NI_ActiveX)
	END_MSG_MAP()
};

//---------------------------------------------------------------------------
/*
	����� NI_ActiveX �̃I�u�W�F�N�g���쐬���ĕԂ������̊֐��ł��B
	��q�� TJSCreateNativeClassForPlugin �̈����Ƃ��ēn���܂��B
*/
static iTJSNativeInstance * TJS_INTF_METHOD Create_NI_ActiveX()
{
	return new NI_ActiveX();
}
//---------------------------------------------------------------------------
/*
	TJS2 �̃l�C�e�B�u�N���X�͈�ӂ� ID �ŋ�ʂ���Ă���K�v������܂��B
	����͌�q�� TJS_BEGIN_NATIVE_MEMBERS �}�N���Ŏ����I�Ɏ擾����܂����A
	���� ID ���i�[����ϐ����ƁA���̕ϐ��������Ő錾���܂��B
	�����l�ɂ͖����� ID ��\�� -1 ���w�肵�Ă��������B
*/
#define TJS_NATIVE_CLASSID_NAME ClassID_ActiveX
static tjs_int32 TJS_NATIVE_CLASSID_NAME = -1;

//---------------------------------------------------------------------------
/*
 *	TJS2 �p�́u�N���X�v���쐬���ĕԂ��֐��ł��B
*/
static iTJSDispatch2 * Create_NC_ActiveX()
{
	// �N���X�I�u�W�F�N�g
	tTJSNativeClassForPlugin * classobj = TJSCreateNativeClassForPlugin(TJS_W("WIN32OLE"), Create_NI_ActiveX);

	// �l�C�e�B�u�����o�[
	TJS_BEGIN_NATIVE_MEMBERS(/*TJS class name*/WIN32OLE)

		// ---------- �������炱�҂؂ɂ����� ------------------
		
		TJS_DECL_EMPTY_FINALIZE_METHOD

		TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(
			/*var.name*/_this,
			/*var.type*/NI_WIN32OLE,
			/*TJS class name*/WIN32OLE)
		{
			// NI_WIN32OLE::Construct �ɂ����e���L�q�ł���̂�
			// �����ł͉������Ȃ�
			return TJS_S_OK;
		}
		TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/WIN32OLE)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/invoke) // invoke ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->invoke(DISPATCH_PROPERTYGET|DISPATCH_METHOD, result, numparams, param);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/invoke)
			
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/set) // set ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->invoke(DISPATCH_PROPERTYPUT, result, numparams, param);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/set)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/get) // get ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->invoke(DISPATCH_PROPERTYGET, result, numparams, param);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/get)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/missing) // missing ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->missing(result, numparams, param);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/missing)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getConstant) // getConstant ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->getConstant(numparams, param, objthis);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/getConstant)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/addEvent) // addEvent ���\�b�h
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_WIN32OLE);
			return _this->addEvent(numparams, param, objthis);
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/addEvent)
			
		// ---------- �����܂ł��҂؂ɂ����� ------------------

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/setExternalUI)
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_ActiveX);
			_this->setExternalUI(numparams, param, objthis);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/setExternalUI)

		// ---------------------------------------------------------------
			
		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/setPos)
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_ActiveX);
			if (numparams < 2) return TJS_E_BADPARAMCOUNT;
			_this->setPos(*param[0], *param[1]);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/setPos)

		TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/setSize)
		{
			TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/NI_ActiveX);
			if (numparams < 2) return TJS_E_BADPARAMCOUNT;
			_this->setSize(*param[0], *param[1]);
			return TJS_S_OK;
		}
		TJS_END_NATIVE_METHOD_DECL(/*func. name*/setSize)
		
		TJS_BEGIN_NATIVE_PROP_DECL(visible) // visible �v���p�e�B
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				*result = _this->getVisible();
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				_this->setVisible((bool)*param);
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(visible)

		TJS_BEGIN_NATIVE_PROP_DECL(left)
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				*result = (tjs_int32)_this->getLeft();
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				_this->setLeft(*param);
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(left)

		TJS_BEGIN_NATIVE_PROP_DECL(top)
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				*result = (tjs_int32)_this->getTop();
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				_this->setTop(*param);
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(top)

		TJS_BEGIN_NATIVE_PROP_DECL(width)
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				*result = (tjs_int32)_this->getWidth();
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				_this->setWidth(*param);
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(width)

		TJS_BEGIN_NATIVE_PROP_DECL(height)
		{
			TJS_BEGIN_NATIVE_PROP_GETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				*result = (tjs_int32)_this->getHeight();
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_GETTER

			TJS_BEGIN_NATIVE_PROP_SETTER
			{
				TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,	/*var. type*/NI_ActiveX);
				_this->setHeight(*param);
				return TJS_S_OK;
			}
			TJS_END_NATIVE_PROP_SETTER
		}
		TJS_END_NATIVE_PROP_DECL(height)
			
	TJS_END_NATIVE_MEMBERS

	return classobj;
}
//---------------------------------------------------------------------------
/*
	TJS_NATIVE_CLASSID_NAME �͈ꉞ undef ���Ă������ق����悢�ł��傤
*/
#undef TJS_NATIVE_CLASSID_NAME
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
#pragma argsused
int WINAPI DllEntryPoint(HINSTANCE hinst, unsigned long reason,
	void* lpReserved)
{
	return 1;
}

// �g���g���̃A�[�J�C�u�ɃA�N�Z�X���邽�߂̏���
void registArchive();
void unregistArchive();

//---------------------------------------------------------------------------
static tjs_int GlobalRefCountAtInit = 0;
static BOOL gOLEInitialized = false;

extern "C" HRESULT _stdcall _export V2Link(iTVPFunctionExporter *exporter)
{
	// �X�^�u�̏�����(�K���L�q����)
	TVPInitImportStub(exporter);

	tTJSVariant val;

	// TJS �̃O���[�o���I�u�W�F�N�g���擾����
	iTJSDispatch2 * global = TVPGetScriptDispatch();

	{
		//-----------------------------------------------------------------------
		// 1 �܂��N���X�I�u�W�F�N�g���쐬
		iTJSDispatch2 * tjsclass = Create_NC_WIN32OLE();
		
		// 2 tjsclass �� tTJSVariant �^�ɕϊ�
		val = tTJSVariant(tjsclass);
		
		// 3 ���ł� val �� tjsclass ��ێ����Ă���̂ŁAtjsclass ��
		//   Release ����
		tjsclass->Release();
		
		
		// 4 global �� PropSet ���\�b�h��p���A�I�u�W�F�N�g��o�^����
		global->PropSet(
			TJS_MEMBERENSURE, // �����o���Ȃ������ꍇ�ɂ͍쐬����悤�ɂ���t���O
			TJS_W("WIN32OLE"), // �����o�� ( ���Ȃ炸 TJS_W( ) �ň͂� )
			NULL, // �q���g ( �{���̓����o���̃n�b�V���l�����ANULL �ł��悢 )
			&val, // �o�^����l
			global // �R���e�L�X�g ( global �ł悢 )
			);
		//-----------------------------------------------------------------------
	}

	{
		//-----------------------------------------------------------------------
		// 1 �܂��N���X�I�u�W�F�N�g���쐬
		iTJSDispatch2 * tjsclass = Create_NC_ActiveX();
		
		// 2 tjsclass �� tTJSVariant �^�ɕϊ�
		val = tTJSVariant(tjsclass);
		
		// 3 ���ł� val �� tjsclass ��ێ����Ă���̂ŁAtjsclass ��
		//   Release ����
		tjsclass->Release();
		
		
		// 4 global �� PropSet ���\�b�h��p���A�I�u�W�F�N�g��o�^����
		global->PropSet(
			TJS_MEMBERENSURE, // �����o���Ȃ������ꍇ�ɂ͍쐬����悤�ɂ���t���O
			TJS_W("ActiveX"), // �����o�� ( ���Ȃ炸 TJS_W( ) �ň͂� )
			NULL, // �q���g ( �{���̓����o���̃n�b�V���l�����ANULL �ł��悢 )
			&val, // �o�^����l
			global // �R���e�L�X�g ( global �ł悢 )
			);
		//-----------------------------------------------------------------------
	}

	
	// - global �� Release ����
	global->Release();

	// �����A�o�^����֐�����������ꍇ�� 1 �` 4 ���J��Ԃ�

	// val ���N���A����B
	// ����͕K���s���B�������Ȃ��� val ���ێ����Ă���I�u�W�F�N�g
	// �� Release ���ꂸ�A���Ɏg�� TVPPluginGlobalRefCount �����m�ɂȂ�Ȃ��B
	val.Clear();

	// ���̎��_�ł� TVPPluginGlobalRefCount �̒l��
	GlobalRefCountAtInit = TVPPluginGlobalRefCount;
	// �Ƃ��čT���Ă����BTVPPluginGlobalRefCount �͂��̃v���O�C������
	// �Ǘ�����Ă��� tTJSDispatch �h���I�u�W�F�N�g�̎Q�ƃJ�E���^�̑��v�ŁA
	// ������ɂ͂���Ɠ������A����������Ȃ��Ȃ��ĂȂ��ƂȂ�Ȃ��B
	// �����Ȃ��ĂȂ���΁A�ǂ����ʂ̂Ƃ���Ŋ֐��Ȃǂ��Q�Ƃ���Ă��āA
	// �v���O�C���͉���ł��Ȃ��ƌ������ƂɂȂ�B

	if (!gOLEInitialized) {
		if (SUCCEEDED(OleInitialize(NULL))) {
			gOLEInitialized = true;
		} else {
			log(L"OLE ���������s");
		}
	}

	// �A�[�J�C�u����
	registArchive();
	
	// ATL�֘A������
	_Module.Init(NULL, NULL);
	AtlAxWinInit();
	
	return S_OK;
}
//---------------------------------------------------------------------------
extern "C" HRESULT _stdcall _export V2Unlink()
{
	// �g���g��������A�v���O�C����������悤�Ƃ���Ƃ��ɌĂ΂��֐��B

	// �������炩�̏����Ńv���O�C��������ł��Ȃ��ꍇ��
	// ���̎��_�� E_FAIL ��Ԃ��悤�ɂ���B
	// �����ł́ATVPPluginGlobalRefCount �� GlobalRefCountAtInit ����
	// �傫���Ȃ��Ă���Ύ��s�Ƃ������Ƃɂ���B
	if(TVPPluginGlobalRefCount > GlobalRefCountAtInit) return E_FAIL;
		// E_FAIL ���A��ƁAPlugins.unlink ���\�b�h�͋U��Ԃ�

	/*
		�������A�N���X�̏ꍇ�A�����Ɂu�I�u�W�F�N�g���g�p���ł���v�Ƃ������Ƃ�
		�m�邷�ׂ�����܂���B��{�I�ɂ́APlugins.unlink �ɂ��v���O�C���̉����
		�댯�ł���ƍl���Ă������� (�������� Plugins.link �Ń����N������A�Ō��
		�Ńv���O�C������������A�v���O�����I���Ɠ����Ɏ����I�ɉ��������̂��g)�B
	*/

	// TJS �̃O���[�o���I�u�W�F�N�g�ɓo�^���� WIN32OLE �N���X�Ȃǂ��폜����

	// - �܂��ATJS �̃O���[�o���I�u�W�F�N�g���擾����
	iTJSDispatch2 * global = TVPGetScriptDispatch();

	// - global �� DeleteMember ���\�b�h��p���A�I�u�W�F�N�g���폜����
	if(global)
	{
		// TJS ���̂����ɉ������Ă����Ƃ��Ȃǂ�
		// global �� NULL �ɂȂ蓾��̂� global �� NULL �łȂ�
		// ���Ƃ��`�F�b�N����

		global->DeleteMember(
			0, // �t���O ( 0 �ł悢 )
			TJS_W("WIN32OLE"), // �����o��
			NULL, // �q���g
			global // �R���e�L�X�g
			);

		global->DeleteMember(
			0, // �t���O ( 0 �ł悢 )
			TJS_W("ActiveX"), // �����o��
			NULL, // �q���g
			global // �R���e�L�X�g
			);
	}
	
	// - global �� Release ����
	if(global) global->Release();

	// ATL �I��
	_Module.Term();

	// �A�[�J�C�u�I��
	unregistArchive();
	
	if (gOLEInitialized) {
		OleUninitialize();
		gOLEInitialized = false;
	}

	// �X�^�u�̎g�p�I��(�K���L�q����)
	TVPUninitImportStub();
	
	return S_OK;
}
//---------------------------------------------------------------------------

