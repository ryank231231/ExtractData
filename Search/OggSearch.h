#pragma once

#include "SearchBase.h"

class COggSearch final : public CSearchBase
{
public:
	COggSearch();
	void Mount(CArcFile* archive) override;

private:
	void OnInit(const SOption* option) override;
};
