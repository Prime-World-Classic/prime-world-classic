/** Observe registered dispatches without exposing or changing private ability ownership. */
static CObj<PFDispatch> B11FindDispatch(PFWorld* world, const PFAbilityInstance* cast)
{
	CObj<PFDispatch> result;
	world->ProcessAddRemove();
	auto find = [&](PF_Core::WorldObjectBase& object) {
		PFDispatch* dispatch = dynamic_cast<PFDispatch*>(&object);
		if (dispatch && !dispatch->IsObjectDead() && dispatch->GetAbility() == cast)
			result = dispatch;
	};
	world->ForAllObjectsBeingStepped(find);
	return result;
}
