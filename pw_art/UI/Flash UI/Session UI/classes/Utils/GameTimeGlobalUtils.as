package Utils
{
	public class GameTimeGlobalUtils
	{
		private static var _currentTime:int = 0;

		public static function get CurrentTime():int
		{
			return _currentTime;
		}

		public static function set CurrentTime(value:int):void
		{
			_currentTime = value;
		}
	}

}
