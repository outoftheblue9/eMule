#pragma once

class CSplashScreen : public CDialog
{
	DECLARE_DYNAMIC(CSplashScreen)

	enum
	{
		IDD = IDD_SPLASH
	};

public:
	explicit CSplashScreen(CWnd *pParent = NULL);   // standard constructor
	virtual	~CSplashScreen();

	void SetStatus(LPCTSTR pszStatus);
	void SetStatus(UINT nResID);

protected:
	CBitmap m_imgSplash;
	CString m_strStatus;
	CRect   m_rcStatus;

	BOOL OnInitDialog();
	void OnPaint();
	BOOL PreTranslateMessage(MSG *pMsg);

	DECLARE_MESSAGE_MAP()
};