#include "ObjectGroup.h"
#include "MyMath.h"
#include <algorithm>
#include <object/base/BaseObject.h>
#include <object/base/BaseObjectManager.h>

namespace Hagine {
bool ObjectGroup::Add(const std::string &objectName)
{
    if (objectName.empty() || Contains(objectName))
    {
        return false;
    }
    members_.push_back(objectName);
    return true;
}

bool ObjectGroup::Add(const BaseObject *pObject)
{
    return pObject ? Add(pObject->GetName()) : false;
}

bool ObjectGroup::Remove(const std::string &objectName)
{
    const auto it = std::find(members_.begin(), members_.end(), objectName);
    if (it == members_.end())
    {
        return false;
    }
    members_.erase(it);
    return true;
}

bool ObjectGroup::Contains(const std::string &objectName) const
{
    return std::find(members_.begin(), members_.end(), objectName) != members_.end();
}

std::vector<BaseObject *> ObjectGroup::GetObjects() const
{
    std::vector<BaseObject *> result;
    result.reserve(members_.size());
    BaseObjectManager *manager = BaseObjectManager::GetInstance();
    for (const std::string &name : members_)
    {
        if (BaseObject *obj = manager->GetObjectByName(name))
        {
            result.push_back(obj);
        }
    }
    return result;
}

BaseObject *ObjectGroup::Find(const std::string &objectName) const
{
    return Contains(objectName) ? BaseObjectManager::GetInstance()->GetObjectByName(objectName) : nullptr;
}

void ObjectGroup::SetVisible(bool visible)
{
    for (BaseObject *obj : GetObjects())
    {
        obj->SetIsModelDraw(visible);
    }
}

bool ObjectGroup::IsAnyVisible() const
{
    for (BaseObject *obj : GetObjects())
    {
        if (obj->GetIsModelDraw())
        {
            return true;
        }
    }
    return false;
}

void ObjectGroup::Translate(const Vector3 &delta)
{
    for (BaseObject *obj : GetObjects())
    {
        // 祖先がメンバーなら祖先と一緒に動く
        bool movedByAncestor = false;
        for (BaseObject *p = obj->GetParent(); p; p = p->GetParent())
        {
            if (Contains(p->GetName()))
            {
                movedByAncestor = true;
                break;
            }
        }
        if (movedByAncestor)
        {
            continue;
        }

        // ワールドの移動量を親のローカルへ戻す
        Vector3 localDelta = delta;
        BaseObject *parent = obj->GetParent();
        if (parent && obj->GetWorldTransform()->inheritTranslation_)
        {
            localDelta = TransformNormal(delta, Inverse(parent->GetWorldMatrix()));
        }
        obj->GetLocalPosition() += localDelta;
        obj->GetWorldTransform()->UpdateMatrix();
    }
}

Vector3 ObjectGroup::GetCenter() const
{
    const std::vector<BaseObject *> objects = GetObjects();
    if (objects.empty())
    {
        return {0.0f, 0.0f, 0.0f};
    }
    Vector3 sum = {0.0f, 0.0f, 0.0f};
    for (BaseObject *obj : objects)
    {
        sum += obj->GetWorldPosition();
    }
    return sum / static_cast<float>(objects.size());
}
} // namespace Hagine
