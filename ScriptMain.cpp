#include <windows.h>
#include <activscp.h>
#include <DispEx.h>
#include "tp_stub.h"
#include <stdio.h>

#pragma warning(disable: 4786)
#include <map>
using namespace std;

#define GLOBAL L"kirikiri"

/**
 * ���O�o�͗p
 */
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

//---------------------------------------------------------------------------

/*
 * Windows Script Host �����p�l�C�e�B�u�C���X�^���X
 */
class NI_ScriptHost : public tTJSNativeInstance, IActiveScriptSite
{
protected:

	/// tjs global �ێ��p
	IDispatchEx *global;

	// ------------------------------------------------------
	// IUnknown ����
	// ------------------------------------------------------
protected:
	ULONG refCount;
public:
	virtual HRESULT __stdcall QueryInterface(REFIID riid, void **ppvObject) {
		*ppvObject = NULL;
		return E_NOTIMPL;
	}
	virtual ULONG _stdcall AddRef(void) {
		return ++refCount;
	}
	virtual ULONG _stdcall Release(void) {
		if(--refCount <= 0) return 0;
		return refCount;
	}

	// ------------------------------------------------------
	// IActiveScriptSite ����
	// ------------------------------------------------------
public:
	virtual HRESULT __stdcall GetLCID(LCID *plcid) {
		return S_OK;
	}

	virtual HRESULT __stdcall GetItemInfo(LPCOLESTR pstrName,
										  DWORD dwReturnMask, IUnknown **ppunkItem, ITypeInfo **ppti) {
		if (ppti) {
			*ppti = NULL;
		}
		if (ppunkItem) {
			*ppunkItem = NULL;
			if (dwReturnMask & SCRIPTINFO_IUNKNOWN) {
				if (!_wcsicmp(GLOBAL, pstrName)) {
					global->AddRef();
					*ppunkItem = global;
				}
			}
		}
		return S_OK;
	}
	
	virtual HRESULT __stdcall GetDocVersionString(BSTR *pbstrVersion) {
		return S_OK;
	}
	
	virtual HRESULT __stdcall OnScriptTerminate(const VARIANT *pvarResult, const EXCEPINFO *ei) {
		return S_OK;
	}
	
	virtual HRESULT __stdcall OnStateChange(SCRIPTSTATE ssScriptState) {
		return S_OK;
	}
	
	virtual HRESULT __stdcall OnScriptError(IActiveScriptError *pscriptError) {
		log(TJS_W("OnScriptError"));
		ttstr errMsg;
		BSTR sourceLine;
		if (pscriptError->GetSourceLineText(&sourceLine) == S_OK) {
			log(TJS_W("source:%ls"), sourceLine);
			::SysFreeString(sourceLine);
		}
		DWORD sourceContext;
		ULONG lineNumber;
		LONG charPosition;
		if (pscriptError->GetSourcePosition(
			&sourceContext,
			&lineNumber,
			&charPosition) == S_OK) {
			log(TJS_W("context:%ld lineNo:%d pos:%d"), sourceContext, lineNumber, charPosition);
		}		
		EXCEPINFO ei;
		memset(&ei, 0, sizeof ei);
		if (pscriptError->GetExceptionInfo(&ei) == S_OK) {
			log(TJS_W("exception code:%x desc:%ls"), ei.wCode, ei.bstrDescription);
		}
		return S_OK;
	}

	virtual HRESULT __stdcall OnEnterScript(void) {
		return S_OK;
	}
	
	virtual HRESULT __stdcall OnLeaveScript(void) {
		return S_OK;
	}

	// ------------------------------------------------------
	// ������
	// ------------------------------------------------------

protected:
	/// �g���q��ProgId �̃}�b�s���O
	map<ttstr, ttstr> extMap;
	// CLSID ��r�p
	struct CompareCLSID : public binary_function<CLSID,CLSID,bool> {
		bool operator() (const CLSID &key1, const CLSID &key2) const {
			return (key1.Data1 < key2.Data1 ||
					key1.Data2 < key2.Data2 ||
					key1.Data3 < key2.Data3 ||
					key1.Data4[0] < key2.Data4[0] ||
					key1.Data4[1] < key2.Data4[1] ||
					key1.Data4[2] < key2.Data4[2] ||
					key1.Data4[3] < key2.Data4[3] ||
					key1.Data4[4] < key2.Data4[4] ||
					key1.Data4[5] < key2.Data4[5] ||
					key1.Data4[6] < key2.Data4[6] ||
					key1.Data4[7] < key2.Data4[7]);
		}
	};
	map<CLSID, IActiveScript*, CompareCLSID> scriptMap;

	/**
	 * �w�肳�ꂽ ActiveScript �G���W�����擾����
	 * @param type �g���q �܂��� progId �܂��� CLSID
	 * @return �G���W���C���^�[�t�F�[�X
	 */
	IActiveScript *getScript(const tjs_char *type) {
		HRESULT hr;
		CLSID   clsid;
		
		// ProgId �܂��� CLSID �̕�����\������G���W���� CLSID �����肷��
		OLECHAR *oleType = ::SysAllocString(type);
		if (FAILED(hr = CLSIDFromProgID(oleType, &clsid))) {
			hr = CLSIDFromString(oleType, &clsid);
		}
		::SysFreeString(oleType);

		if (SUCCEEDED(hr)) {
			map<CLSID, IActiveScript*, CompareCLSID>::const_iterator n = scriptMap.find(clsid);
			if (n != scriptMap.end()) {
				// ���łɎ擾�ς݂̃G���W���̏ꍇ�͂����Ԃ�
				return n->second;
			} else {
				// �V�K�擾
				IActiveScript *pScript;
				hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_IActiveScript, (void**)&pScript);
				if (SUCCEEDED(hr)) {
					IActiveScriptParse *pScriptParse;
					if (SUCCEEDED(pScript->QueryInterface(IID_IActiveScriptParse, (void **)&pScriptParse))) {
						// ActiveScriptSite ��o�^
						pScript->SetScriptSite(this);
						// �O���[�o���ϐ��̖��O��o�^
						pScript->AddNamedItem(GLOBAL, SCRIPTITEM_ISVISIBLE | SCRIPTITEM_ISSOURCE);
						// ������
						pScriptParse->InitNew();
						pScriptParse->Release();
						scriptMap[clsid] = pScript;
						return pScript;
					} else {
						log(TJS_W("QueryInterface IActipveScriptParse failed %s"), type);
						pScript->Release();
					}
				} else {
					log(TJS_W("CoCreateInstance failed %s"), type);
				}
			}
		} else {
			log(TJS_W("bad ProgId/CLSID %s"), type);
		}
		return NULL;
	}
	
public:
	/**
	 * �R���X�g���N�^
	 */
	NI_ScriptHost() {
		// global �̎擾
		iTJSDispatch2 * tjsGlobal = TVPGetScriptDispatch();
		global = new IDispatchWrapper(tjsGlobal);
		tjsGlobal->Release();
		// �g���q�ɑ΂��� ProgId �̃}�b�s���O�̓o�^
		extMap["js"]  = "JScript";
		extMap["vbs"] = "VBScript";
		extMap["pl"]  = "PerlScript";
		extMap["rb"]  = "RubyScript";
	}

	/**
	 * �f�X�g���N�^
	 */
	~NI_ScriptHost() {
		// �G���W���̊J��
 		map<CLSID, IActiveScript*, CompareCLSID>::iterator i = scriptMap.begin();
		while (i != scriptMap.end()) {
			i->second->Close();
			i->second->Release();
			i = scriptMap.erase(i);
		}
		// global ���J��
		global->Release();
	}

	/**
	 * �g���q�� ProgId �ɕϊ�����
	 * @param exe �g���q
	 * @return ProgId
	 */
	const tjs_char *getProgId(const tjs_char *ext) {
		ttstr extStr(ext);
		extStr.ToLowerCase();
		map<ttstr, ttstr>::const_iterator n = extMap.find(extStr);
		if (n != extMap.end()) {
			return n->second.c_str();
		}
		return NULL;
	}

	/**
	 * �g���q�� ProgId �̑g��ǉ��o�^����
	 * @param exe �g���q
	 * @param progId ProgId
	 */
	void addProgId(const tjs_char *ext, const tjs_char *progId) {
		ttstr extStr(ext);
		extStr.ToLowerCase();
		extMap[extStr] = progId;
	}

	/**
	 * �X�N���v�g�̎��s
	 * @param progId �X�N���v�g�̎��
	 * @param script �X�N���v�g�{��
	 * @param result ���ʊi�[��
	 */
	tjs_error exec(const tjs_char *progId, const tjs_char *script, tTJSVariant *result) {
		IActiveScript *pScript = getScript(progId);
		if (pScript) {
			IActiveScriptParse *pScriptParse;
			if (SUCCEEDED(pScript->QueryInterface(IID_IActiveScriptParse, (void **)&pScriptParse))) {
				
				// ���ʊi�[�p
				HRESULT hr;
				EXCEPINFO ei;
				VARIANT rs;
				memset(&ei, 0, sizeof ei);

				BSTR pParseText = ::SysAllocString(script);
				if (SUCCEEDED(hr = pScriptParse->ParseScriptText(pParseText, GLOBAL, NULL, NULL, 0, 0, 0L, &rs, &ei))) {
					hr = pScript->SetScriptState(SCRIPTSTATE_CONNECTED);
				}
				::SysFreeString(pParseText);
				
				switch (hr) {
				case S_OK:
					if (result) {
						IDispatchWrapper::storeVariant(*result, rs);
					}
					return TJS_S_OK;
				case DISP_E_EXCEPTION:
					log(TJS_W("exception code:%x desc:%ls"), ei.wCode, ei.bstrDescription);
					TVPThrowExceptionMessage(TJS_W("exception"));
					break;
				case E_POINTER:
					TVPThrowExceptionMessage(TJS_W("memory error"));
					break;
				case E_INVALIDARG:
					return TJS_E_INVALIDPARAM;
				case E_NOTIMPL:
					return TJS_E_NOTIMPL;
				case E_UNEXPECTED:
					return TJS_E_ACCESSDENYED;
				default:
					log(TJS_W("error:%x"), hr);
					return TJS_E_FAIL;
				}
			}
		}
		return TJS_E_FAIL;
	}

	/**
	 * �X�N���v�g�̎��s
	 * @param progId �X�N���v�g�̎��
	 * @param script �X�N���v�g�{��
	 * @param result ���ʊi�[��
	 */
	tjs_error execStorage(const tjs_char *progId, const tjs_char *filename, tTJSVariant *result) {
		
		iTJSTextReadStream * stream = TVPCreateTextStreamForRead(filename, TJS_W(""));
		tjs_error ret;
		try {
			ttstr data;
			stream->Read(data, 0);
			ret = exec(progId, data.c_str(), result);
		}
		catch(...)
		{
			stream->Destruct();
			throw;
		}
		stream->Destruct();
		return ret;
	}
};

// �N���XID
static tjs_int32 ClassID_ScriptHost = -1;

/**
 * Windows Script Host ���s�����p���\�b�h
 * exec(script [,lang])
 * �������Q�ȏ゠��ꍇ�́A�Q�ڂ�����w��Ƃ݂Ȃ��ăX�N���v�g�����s����B
 * �����łȂ��ꍇ�� tjs �̃X�N���v�g�Ƃ݂Ȃ��Ď��s����B
 */
class tExecFunction : public tTJSDispatch
{
protected:
	/// ���̃��\�b�h
	iTJSDispatch2 *originalFunction;
public:	
	/// �R���X�g���N�^
	tExecFunction(tTJSVariant &original) {
		originalFunction = original.AsObject();
	}
	/// �f�X�g���N�^
	~tExecFunction() {
		originalFunction->Release();
	}
	
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32 flag, const tjs_char * membername, tjs_uint32 *hint,
		tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {

		// �l�C�e�B�u�I�u�W�F�N�g�̎擾
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		NI_ScriptHost *_this;
		tjs_error hr;
		hr = objthis->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
											ClassID_ScriptHost, (iTJSNativeInstance**)&_this); \
		if (TJS_FAILED(hr)) return TJS_E_NATIVECLASSCRASH;
		
		if (membername) return TJS_E_MEMBERNOTFOUND;
		if (numparams == 0) return TJS_E_BADPARAMCOUNT;

		if (numparams > 1) {
			return _this->exec(param[1]->GetString(), param[0]->GetString(), result);
		} else {
			return originalFunction->FuncCall(flag, membername, hint, result, numparams, param, objthis);
		}
	}
};

/**
 * Windows Script Host ���s�����p���\�b�h
 * execStorage(storage [,lang])
 * �������Q�ȏ゠��ꍇ�́A�Q�ڂ�����w��Ƃ݂Ȃ��ăX�g���[�W�����s����B
 * �����łȂ��ꍇ�́Astorage �̊g���q�𒲂ׁA�Y�����錾�ꂪ���݂���΂��̌���ŁA
 * ���݂��Ȃ���� tjs �̃X�g���[�W�Ƃ݂Ȃ��Ď��s����B
 */
class tExecStorageFunction : public tTJSDispatch
{
protected:
	/// ���̃��\�b�h
	iTJSDispatch2 *originalFunction;
public:	
	/// �R���X�g���N�^
	tExecStorageFunction(tTJSVariant &original) {
		originalFunction = original.AsObject();
	}
	/// �f�X�g���N�^
	~tExecStorageFunction() {
		originalFunction->Release();
	}

	const tjs_char *getProgId(NI_ScriptHost *_this, tjs_int numparams, tTJSVariant **param) {
		if (numparams > 1) {
			return param[1]->GetString();
		} else {
			const tjs_char *ext = wcsrchr(param[0]->GetString(), '.');
			if (ext) {
				return _this->getProgId(ext + 1);
			}
		}
		return NULL;
	}
	
	tjs_error TJS_INTF_METHOD FuncCall(
		tjs_uint32 flag, const tjs_char * membername, tjs_uint32 *hint,
		tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {

		// �l�C�e�B�u�I�u�W�F�N�g�̎擾
		if (!objthis) return TJS_E_NATIVECLASSCRASH;
		NI_ScriptHost *_this;
		tjs_error hr;
		hr = objthis->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
											ClassID_ScriptHost, (iTJSNativeInstance**)&_this); \
		if (TJS_FAILED(hr)) return TJS_E_NATIVECLASSCRASH;
		
		if (membername) return TJS_E_MEMBERNOTFOUND;
		if (numparams == 0) return TJS_E_BADPARAMCOUNT;

		const tjs_char *progId;
		if ((progId = getProgId(_this, numparams, param))) {
			return _this->execStorage(progId, param[0]->GetString(), result);
		} else {
			return originalFunction->FuncCall(flag, membername, hint, result, numparams, param, objthis);
		}
	}
	
};


//---------------------------------------------------------------------------
#pragma argsused
int WINAPI DllEntryPoint(HINSTANCE hinst, unsigned long reason,
	void* lpReserved)
{
	return 1;
}

extern void registArchive();
extern void unregistArchive();

//---------------------------------------------------------------------------
static tjs_int GlobalRefCountAtInit = 0;
static BOOL gOLEInitialized = false;

extern "C" HRESULT _stdcall _export V2Link(iTVPFunctionExporter *exporter)
{
	// �X�^�u�̏�����(�K���L�q����)
	TVPInitImportStub(exporter);

	{
		// �O���[�o��
		iTJSDispatch2 * global = TVPGetScriptDispatch();

		// Scripts �I�u�W�F�N�g���擾
		tTJSVariant varScripts;
		TVPExecuteExpression(TJS_W("Scripts"), &varScripts);
		iTJSDispatch2 *dispatch = varScripts.AsObjectNoAddRef();
		if (dispatch) {

			// �N���X�I�u�W�F�N�g����
			ClassID_ScriptHost = TJSRegisterNativeClass(TJS_W("ScriptHost"));
			iTJSNativeInstance *ni = new NI_ScriptHost();
			dispatch->NativeInstanceSupport(TJS_NIS_REGISTER, ClassID_ScriptHost, &ni);

			// �����p���\�b�h��������

			// exec
			{
				const tjs_char *memberName = TJS_W("exec");
				tTJSVariant var;
				dispatch->PropGet(0, memberName, NULL, &var, dispatch);
				tTJSDispatch *method = new tExecFunction(var);
				var = tTJSVariant(method, dispatch);
				method->Release();
				dispatch->PropSet(
					TJS_MEMBERENSURE, // �����o���Ȃ������ꍇ�ɂ͍쐬����悤�ɂ���t���O
					memberName, // �����o�� ( ���Ȃ炸 TJS_W( ) �ň͂� )
					NULL, // �q���g ( �{���̓����o���̃n�b�V���l�����ANULL �ł��悢 )
					&var, // �o�^����l
					dispatch // �R���e�L�X�g
					);
			}

			// execStorage
			{
				const tjs_char *memberName = TJS_W("execStorage");
				tTJSVariant var;
				dispatch->PropGet(0, memberName, NULL, &var, dispatch);
				tTJSDispatch *method = new tExecStorageFunction(var);
				var = tTJSVariant(method, dispatch);
				method->Release();
				dispatch->PropSet(
					TJS_MEMBERENSURE, // �����o���Ȃ������ꍇ�ɂ͍쐬����悤�ɂ���t���O
					memberName, // �����o�� ( ���Ȃ炸 TJS_W( ) �ň͂� )
					NULL, // �q���g ( �{���̓����o���̃n�b�V���l�����ANULL �ł��悢 )
					&var, // �o�^����l
					dispatch // �R���e�L�X�g
					);
			}
		}
		global->Release();
	}
	
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
			log(TJS_W("OLE ���������s"));
		}
	}
	
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

	if (gOLEInitialized) {
		OleUninitialize();
		gOLEInitialized = false;
	}
	
	// �X�^�u�̎g�p�I��(�K���L�q����)
	TVPUninitImportStub();
	
	return S_OK;
}
