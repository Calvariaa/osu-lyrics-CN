#pragma once

__interface Base
{
public:
    void Release();
};

void ReleaseBaseObject(Base *pObject); 
/* Utils.cpp�� ������. ������Ʈ�� �ִ��� ������ Ȯ���� �Ŀ�.
   �����ϰ� ���ŵ�. ����=>ReleaseBaseObject(Object);  */