/** World presentation publishes actual movement speed, not stat speed or a guessed rate. */
static void LocomotionPresentationB13(Checks& check)
{
	const int before=check.count,failed=check.failed;
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto unit=CommonCreepB11(f.world);
	for (float speed : {2.5f,10.f,0.f,5.f}) {
		const float actual=unit->SetUnitSpeed(speed);
		vector<LinuxDynamicWorldMarker> markers; f.world->GetLinuxDynamicWorldMarkers(markers,10000);
		bool found=false;
		for (const auto& marker : markers) if (marker.objectId==unit->GetObjectId()) found=marker.moveSpeed==actual;
		check(found,"B13 marker carries current unit speed including zero and resume");
		check(unit->GetUnitSpeed()==actual,"B13 visual snapshot never mutates simulation speed");
	}
	std::printf("Locomotion presentation gameplay: checks=%d failures=%d\n",check.count-before,check.failed-failed);
}
