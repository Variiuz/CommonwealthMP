package
{
	import flash.display.MovieClip;
	import flash.events.Event;

	// Companion SWF for CommonwealthMP menu rows (title + pause).
	// Loaded into Interface/MainMenu.swf by the F4SE plugin (scaleform_menu.cpp).
	public class CommonwealthMP_Menu extends MovieClip
	{
		private static const IDX_JOIN:int = 601;
		private static const IDX_HOST:int = 602;
		private static const IDX_DISCONNECT:int = 603;

		private static const TXT_JOIN:String = "JOIN";
		private static const TXT_HOST:String = "HOST";
		private static const TXT_DISCONNECT:String = "DISCONNECT";

		private var menuClip:MovieClip = null;
		private var listenerAttached:Boolean = false;
		private var framesWaited:int = 0;
		private var titleMode:Boolean = false;
		private static const MAX_WAIT_FRAMES:int = 60;

		public function CommonwealthMP_Menu()
		{
			super();
			addEventListener(Event.ENTER_FRAME, onEnterFrame);
		}

		private function onEnterFrame(e:Event):void
		{
			if (!stage)
			{
				return;
			}

			var host:Object = stage.getChildAt(0);
			if (!host)
			{
				return;
			}

			var menu:MovieClip = host["Menu_mc"];
			if (!menu)
			{
				return;
			}

			titleMode = !Boolean(menu["PauseMode"]);

			if (!menu["MainPanel_mc"] || !menu["MainPanel_mc"].List_mc)
			{
				return;
			}

			menuClip = menu;
			var list:Object = menu["MainPanel_mc"].List_mc;
			var entries:Array = list.entryList as Array;
			if (!entries)
			{
				return;
			}

			if (!listenerAttached)
			{
				menuClip.addEventListener("BSScrollingList::itemPress", onItemPress);
				listenerAttached = true;
			}

			framesWaited++;
			var others:int = countNonCmpRows(entries);
			if (others == 0 && framesWaited < MAX_WAIT_FRAMES)
			{
				return;
			}

			syncRows(host, entries, list);
			removeEventListener(Event.ENTER_FRAME, onEnterFrame);
		}

		private function countNonCmpRows(entries:Array):int
		{
			var count:int = 0;
			for (var i:int = 0; i < entries.length; i++)
			{
				if (!isCmpRow(entries[i]))
				{
					count++;
				}
			}
			return count;
		}

		private function isCmpRow(entry:Object):Boolean
		{
			if (!entry)
			{
				return false;
			}
			var idx:int = int(entry.index);
			return idx == IDX_JOIN || idx == IDX_HOST || idx == IDX_DISCONNECT;
		}

		private function removeCmpRows(entries:Array):void
		{
			for (var i:int = entries.length - 1; i >= 0; i--)
			{
				if (isCmpRow(entries[i]))
				{
					entries.splice(i, 1);
				}
			}
		}

		private function syncRows(host:Object, entries:Array, list:Object):void
		{
			removeCmpRows(entries);

			var connected:Boolean = false;
			if (host["cmp"] && host["cmp"].IsConnected !== undefined)
			{
				connected = Boolean(host["cmp"].IsConnected());
			}

			var pos:int = 1;
			if (host["cmp"] && host["cmp"].buttonPos !== undefined)
			{
				pos = int(host["cmp"].buttonPos);
			}

			var others:int = entries.length;
			var slot:int = (pos < 0) ? others : ((pos < others) ? pos : others);

			if (connected)
			{
				entries.splice(slot, 0, { "text": TXT_DISCONNECT, "index": IDX_DISCONNECT });
			}
			else
			{
				entries.splice(slot, 0, { "text": TXT_JOIN, "index": IDX_JOIN });
				entries.splice(slot + 1, 0, { "text": TXT_HOST, "index": IDX_HOST });
			}

			list.InvalidateData();
		}

		private function onItemPress(e:Event):void
		{
			if (!menuClip || !menuClip.MainPanel_mc || !menuClip.MainPanel_mc.List_mc)
			{
				return;
			}

			var sel:Object = menuClip.MainPanel_mc.List_mc.selectedEntry;
			if (!sel)
			{
				return;
			}

			if (!stage)
			{
				return;
			}

			var host:Object = stage.getChildAt(0);
			if (!host || !host["cmp"])
			{
				return;
			}

			switch (int(sel.index))
			{
				case IDX_JOIN:
					if (titleMode)
					{
						host["cmp"].OpenJoinTitle();
					}
					else
					{
						host["cmp"].OpenJoin();
					}
					break;
				case IDX_HOST:
					if (titleMode)
					{
						host["cmp"].OpenHostTitle();
					}
					else
					{
						host["cmp"].OpenHost();
					}
					break;
				case IDX_DISCONNECT:
					host["cmp"].Disconnect();
					break;
			}
		}
	}
}
